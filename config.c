/*
 * config.c - configuration management
 *
 * Copyright © 2007 Julien Danjou <julien@danjou.info>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/**
 * \defgroup ui_callback
 */

#include <confuse.h>
#include <X11/keysym.h>

#include "awesome.h"
#include "layout.h"
#include "statusbar.h"
#include "util.h"
#include "rules.h"
#include "screen.h"
#include "layouts/tile.h"
#include "layouts/floating.h"
#include "layouts/max.h"

#define AWESOME_CONFIG_FILE ".awesomerc" 

static XColor initxcolor(Display *, int, const char *);
static unsigned int get_numlockmask(Display *);

/** Link a name to a key symbol */
typedef struct
{
    const char *name;
    KeySym keysym;
} KeyMod;

/** Link a name to a mouse button symbol */
typedef struct
{
    const char *name;
    unsigned int button;
} MouseButton;

extern const NameFuncLink UicbList[];

/** List of keyname and corresponding X11 mask codes */
static const KeyMod KeyModList[] =
{
    {"Shift", ShiftMask},
    {"Lock", LockMask},
    {"Control", ControlMask},
    {"Mod1", Mod1Mask},
    {"Mod2", Mod2Mask},
    {"Mod3", Mod3Mask},
    {"Mod4", Mod4Mask},
    {"Mod5", Mod5Mask},
    {NULL, NoSymbol}
};

/** List of button name and corresponding X11 mask codes */
static const MouseButton MouseButtonList[] =
{
    {"1", Button1},
    {"2", Button2},
    {"3", Button3},
    {"4", Button4},
    {"5", Button5},
    {NULL, 0}
};
/** List of available layouts and link between name and functions */
static const NameFuncLink LayoutsList[] =
{
    {"tile", layout_tile},
    {"tileleft", layout_tileleft},
    {"max", layout_max},
    {"floating", layout_floating},
    {NULL, NULL}
};

/** Lookup for a key mask from its name
 * \param keyname Key name
 * \return Key mask or 0 if not found
 */
static KeySym
key_mask_lookup(const char *keyname)
{
    int i;

    if(keyname)
        for(i = 0; KeyModList[i].name; i++)
            if(!a_strcmp(keyname, KeyModList[i].name))
                return KeyModList[i].keysym;

    return NoSymbol;
}

/** Lookup for a mouse button from its name
 * \param button Mouse button name
 * \return Mouse button or 0 if not found
 */
static unsigned int
mouse_button_lookup(const char *button)
{
    int i;
    
    if(button)
        for(i = 0; MouseButtonList[i].name; i++)
            if(!a_strcmp(button, MouseButtonList[i].name))
                return MouseButtonList[i].button;

    return 0;
}

static Button *
parse_mouse_bindings(cfg_t * cfg, const char *secname, Bool handle_arg)
{
    unsigned int i, j;
    cfg_t *cfgsectmp;
    Button *b = NULL, *head = NULL;

    /* Mouse: layout click bindings */
    for(i = 0; i < cfg_size(cfg, secname); i++)
    {
        /* init first elem */
        if(i == 0)
            head = b = p_new(Button, 1);

        cfgsectmp = cfg_getnsec(cfg, secname, i);
        for(j = 0; j < cfg_size(cfgsectmp, "modkey"); j++)
            b->mod |= key_mask_lookup(cfg_getnstr(cfgsectmp, "modkey", j));
        b->button = mouse_button_lookup(cfg_getstr(cfgsectmp, "button"));
        b->func = name_func_lookup(cfg_getstr(cfgsectmp, "command"), UicbList);
        if(!b->func)
            warn("unknown command %s\n", cfg_getstr(cfgsectmp, "command"));
        if(handle_arg)
            b->arg = a_strdup(cfg_getstr(cfgsectmp, "arg"));
        else
            b->arg = NULL;

        /* switch to next elem or finalize the list */
        if(i < cfg_size(cfg, secname) - 1)
        {
            b->next = p_new(Button, 1);
            b = b->next;
        }
        else
            b->next = NULL;
    }

    return head;
}

/** Parse configuration file and initialize some stuff
 * \param disp Display ref
 * \param scr Screen number
 */
void
parse_config(const char *confpatharg, awesome_config *awesomeconf)
{
    static cfg_opt_t general_opts[] =
    {
        CFG_INT((char *) "border", 1, CFGF_NONE),
        CFG_INT((char *) "snap", 8, CFGF_NONE),
        CFG_BOOL((char *) "resize_hints", cfg_false, CFGF_NONE),
        CFG_INT((char *) "opacity_unfocused", 100, CFGF_NONE),
        CFG_BOOL((char *) "focus_move_pointer", cfg_false, CFGF_NONE),
        CFG_BOOL((char *) "allow_lower_floats", cfg_false, CFGF_NONE),
        CFG_STR((char *) "font", (char *) "mono-12", CFGF_NONE),
        CFG_END()
    };
    static cfg_opt_t colors_opts[] =
    {
        CFG_STR((char *) "normal_border", (char *) "#111111", CFGF_NONE),
        CFG_STR((char *) "normal_bg", (char *) "#111111", CFGF_NONE),
        CFG_STR((char *) "normal_fg", (char *) "#eeeeee", CFGF_NONE),
        CFG_STR((char *) "focus_border", (char *) "#6666ff", CFGF_NONE),
        CFG_STR((char *) "focus_bg", (char *) "#6666ff", CFGF_NONE),
        CFG_STR((char *) "focus_fg", (char *) "#ffffff", CFGF_NONE),
        CFG_STR((char *) "tab_border", (char *) "#ff0000", CFGF_NONE),
        CFG_END()
    };
    static cfg_opt_t statusbar_opts[] =
    {
        CFG_STR((char *) "position", (char *) "top", CFGF_NONE),
        CFG_END()
    };
    static cfg_opt_t tag_opts[] =
    {
        CFG_STR((char *) "layout", (char *) "tile", CFGF_NONE),
        CFG_FLOAT((char *) "mwfact", 0.5, CFGF_NONE),
        CFG_INT((char *) "nmaster", 1, CFGF_NONE),
        CFG_INT((char *) "ncol", 1, CFGF_NONE),
        CFG_END()
    };
    static cfg_opt_t tags_opts[] =
    {
        CFG_SEC((char *) "tag", tag_opts, CFGF_TITLE | CFGF_MULTI),
        CFG_END()
    };
    static cfg_opt_t layout_opts[] =
    {
        CFG_STR((char *) "symbol", (char *) "???", CFGF_NONE),
        CFG_END()
    };
    static cfg_opt_t layouts_opts[] =
    {
        CFG_SEC((char *) "layout", layout_opts, CFGF_TITLE | CFGF_MULTI),
        CFG_END()
    };
    static cfg_opt_t padding_opts[] =
    {
        CFG_INT((char *) "top", 0, CFGF_NONE),
        CFG_INT((char *) "bottom", 0, CFGF_NONE),
        CFG_INT((char *) "right", 0, CFGF_NONE),
        CFG_INT((char *) "left", 0, CFGF_NONE),
        CFG_END()
    };
    static cfg_opt_t screen_opts[] =
    {
        CFG_SEC((char *) "general", general_opts, CFGF_NONE),
        CFG_SEC((char *) "statusbar", statusbar_opts, CFGF_NONE),
        CFG_SEC((char *) "tags", tags_opts, CFGF_NONE),
        CFG_SEC((char *) "colors", colors_opts, CFGF_NONE),
        CFG_SEC((char *) "layouts", layouts_opts, CFGF_NONE),
        CFG_SEC((char *) "padding", padding_opts, CFGF_NONE),
    };
    static cfg_opt_t rule_opts[] =
    {
        CFG_STR((char *) "name", (char *) "", CFGF_NONE),
        CFG_STR((char *) "tags", (char *) "", CFGF_NONE),
        CFG_BOOL((char *) "float", cfg_false, CFGF_NONE),
        CFG_INT((char *) "screen", RULE_NOSCREEN, CFGF_NONE),
        CFG_END()
    };
    static cfg_opt_t rules_opts[] =
    {
        CFG_SEC((char *) "rule", rule_opts, CFGF_MULTI),
        CFG_END()
    };
    static cfg_opt_t key_opts[] =
    {
        CFG_STR_LIST((char *) "modkey", (char *) "{Mod4}", CFGF_NONE),
        CFG_STR((char *) "key", (char *) "None", CFGF_NONE),
        CFG_STR((char *) "command", (char *) "", CFGF_NONE),
        CFG_STR((char *) "arg", NULL, CFGF_NONE),
        CFG_END()
    };
    static cfg_opt_t keys_opts[] =
    {
        CFG_SEC((char *) "key", key_opts, CFGF_MULTI),
        CFG_END()
    };
    static cfg_opt_t mouse_tag_opts[] =
    {
        CFG_STR_LIST((char *) "modkey", (char *) "{}", CFGF_NONE),
        CFG_STR((char *) "button", (char *) "None", CFGF_NONE),
        CFG_STR((char *) "command", (char *) "", CFGF_NONE),
        CFG_END()
    };
    static cfg_opt_t mouse_generic_opts[] =
    {
        CFG_STR_LIST((char *) "modkey", (char *) "{}", CFGF_NONE),
        CFG_STR((char *) "button", (char *) "None", CFGF_NONE),
        CFG_STR((char *) "command", (char *) "", CFGF_NONE),
        CFG_STR((char *) "arg", NULL, CFGF_NONE),
        CFG_END()
    };
    static cfg_opt_t mouse_opts[] =
    {
        CFG_SEC((char *) "tag", mouse_tag_opts, CFGF_MULTI),
        CFG_SEC((char *) "layout", mouse_generic_opts, CFGF_MULTI),
        CFG_SEC((char *) "title", mouse_generic_opts, CFGF_MULTI),
        CFG_SEC((char *) "root", mouse_generic_opts, CFGF_MULTI),
        CFG_SEC((char *) "client", mouse_generic_opts, CFGF_MULTI),
        CFG_END()
    };
    static cfg_opt_t opts[] =
    {
        CFG_SEC((char *) "screen", screen_opts, CFGF_TITLE | CFGF_MULTI),
        CFG_SEC((char *) "rules", rules_opts, CFGF_NONE),
        CFG_SEC((char *) "keys", keys_opts, CFGF_NONE),
        CFG_SEC((char *) "mouse", mouse_opts, CFGF_NONE),
        CFG_END()
    };
    cfg_t *cfg, *cfg_general, *cfg_colors, *cfg_screen, *cfg_statusbar, *cfg_tags,
          *cfg_layouts, *cfg_rules, *cfg_keys, *cfg_mouse, *cfgsectmp, *cfg_padding;
    int i = 0, k = 0, ret, screen;
    unsigned int j = 0, l = 0;
    const char *tmp, *homedir;
    char *confpath, buf[2];
    ssize_t confpath_len;
    Key *key = NULL;
    Rule *rule = NULL;

    if(confpatharg)
        confpath = a_strdup(confpatharg);
    else
    {
        homedir = getenv("HOME");
        confpath_len = a_strlen(homedir) + a_strlen(AWESOME_CONFIG_FILE) + 2;
        confpath = p_new(char, confpath_len);
        a_strcpy(confpath, confpath_len, homedir);
        a_strcat(confpath, confpath_len, "/");
        a_strcat(confpath, confpath_len, AWESOME_CONFIG_FILE);
    }

    awesomeconf->configpath = a_strdup(confpath);

    cfg = cfg_init(opts, CFGF_NONE);

    ret = cfg_parse(cfg, confpath);
    if(ret == CFG_FILE_ERROR)
    {
        perror("awesome: parsing configuration file failed");
        cfg_parse_buf(cfg, AWESOME_DEFAULT_CONFIG);
    }
    else if(ret == CFG_PARSE_ERROR)
        cfg_error(cfg, "awesome: parsing configuration file %s failed.\n", confpath);

    /* get the right screen section */
    for(screen = 0; screen < get_screen_count(awesomeconf->display); screen++)
    {
        a_strcpy(awesomeconf->screens[screen].statustext,
                 sizeof(awesomeconf->screens[screen].statustext),
                 "awesome-" VERSION " (" RELEASE ")");
        snprintf(buf, sizeof(buf), "%d", screen);
        cfg_screen = cfg_gettsec(cfg, "screen", buf);
        if(!cfg_screen)
            cfg_screen = cfg_getsec(cfg, "screen");

        if(!cfg_screen)
        {
            warn("parsing configuration file failed, no screen section found");
            cfg_parse_buf(cfg, AWESOME_DEFAULT_CONFIG);
            cfg_screen = cfg_getsec(cfg, "screen");
        }

        /* get screen specific sections */
        cfg_statusbar = cfg_getsec(cfg_screen, "statusbar");
        cfg_tags = cfg_getsec(cfg_screen, "tags");
        cfg_colors = cfg_getsec(cfg_screen, "colors");
        cfg_general = cfg_getsec(cfg_screen, "general");
        cfg_layouts = cfg_getsec(cfg_screen, "layouts");
        cfg_padding = cfg_getsec(cfg_screen, "padding");


        /* General section */
        awesomeconf->screens[screen].borderpx = cfg_getint(cfg_general, "border");
        awesomeconf->screens[screen].snap = cfg_getint(cfg_general, "snap");
        awesomeconf->screens[screen].resize_hints = cfg_getbool(cfg_general, "resize_hints");
        awesomeconf->screens[screen].opacity_unfocused = cfg_getint(cfg_general, "opacity_unfocused");
        awesomeconf->screens[screen].focus_move_pointer = cfg_getbool(cfg_general, "focus_move_pointer");
        awesomeconf->screens[screen].allow_lower_floats = cfg_getbool(cfg_general, "allow_lower_floats");
        awesomeconf->screens[screen].font = XftFontOpenName(awesomeconf->display,
                                                            get_phys_screen(awesomeconf->display, screen),
                                                            cfg_getstr(cfg_general, "font"));
        if(!awesomeconf->screens[screen].font)
            eprint("awesome: cannot init font\n");
        /* Colors */
        awesomeconf->screens[screen].colors_normal[ColBorder] = initxcolor(awesomeconf->display, get_phys_screen(awesomeconf->display, screen), cfg_getstr(cfg_colors, "normal_border"));
        awesomeconf->screens[screen].colors_normal[ColBG] = initxcolor(awesomeconf->display, get_phys_screen(awesomeconf->display, screen), cfg_getstr(cfg_colors, "normal_bg"));
        awesomeconf->screens[screen].colors_normal[ColFG] = initxcolor(awesomeconf->display, get_phys_screen(awesomeconf->display, screen), cfg_getstr(cfg_colors, "normal_fg"));
        awesomeconf->screens[screen].colors_selected[ColBorder] = initxcolor(awesomeconf->display, get_phys_screen(awesomeconf->display, screen), cfg_getstr(cfg_colors, "focus_border"));
        awesomeconf->screens[screen].colors_selected[ColBG] = initxcolor(awesomeconf->display, get_phys_screen(awesomeconf->display, screen), cfg_getstr(cfg_colors, "focus_bg"));
        awesomeconf->screens[screen].colors_selected[ColFG] = initxcolor(awesomeconf->display, get_phys_screen(awesomeconf->display, screen), cfg_getstr(cfg_colors, "focus_fg"));

        /* Statusbar */
        tmp = cfg_getstr(cfg_statusbar, "position");

        if(tmp && !a_strncmp(tmp, "off", 6))
            awesomeconf->screens[screen].statusbar.dposition = BarOff;
        else if(tmp && !a_strncmp(tmp, "bottom", 6))
            awesomeconf->screens[screen].statusbar.dposition = BarBot;
        else if(tmp && !a_strncmp(tmp, "right", 5))
            awesomeconf->screens[screen].statusbar.dposition = BarRight;
        else if(tmp && !a_strncmp(tmp, "left", 4))
            awesomeconf->screens[screen].statusbar.dposition = BarLeft;
        else
            awesomeconf->screens[screen].statusbar.dposition = BarTop;

        awesomeconf->screens[screen].statusbar.position = awesomeconf->screens[screen].statusbar.dposition;

        /* Layouts */
        awesomeconf->screens[screen].nlayouts = cfg_size(cfg_layouts, "layout");
        awesomeconf->screens[screen].layouts = p_new(Layout, awesomeconf->screens[screen].nlayouts);
        for(i = 0; i < awesomeconf->screens[screen].nlayouts; i++)
        {
            cfgsectmp = cfg_getnsec(cfg_layouts, "layout", i);
            awesomeconf->screens[screen].layouts[i].arrange = name_func_lookup(cfg_title(cfgsectmp), LayoutsList);
            if(!awesomeconf->screens[screen].layouts[i].arrange)
            {
                warn("unknown layout %s in configuration file\n", cfg_title(cfgsectmp));
                awesomeconf->screens[screen].layouts[i].symbol = NULL;
                continue;
            }
            awesomeconf->screens[screen].layouts[i].symbol = a_strdup(cfg_getstr(cfgsectmp, "symbol"));
        }

        if(!awesomeconf->screens[screen].nlayouts)
            eprint("awesome: fatal: no default layout available\n");

        /* Tags */
        awesomeconf->screens[screen].ntags = cfg_size(cfg_tags, "tag");
        awesomeconf->screens[screen].tags = p_new(Tag, awesomeconf->screens[screen].ntags);
        for(i = 0; i < awesomeconf->screens[screen].ntags; i++)
        {
            cfgsectmp = cfg_getnsec(cfg_tags, "tag", i);
            awesomeconf->screens[screen].tags[i].name = a_strdup(cfg_title(cfgsectmp));
            awesomeconf->screens[screen].tags[i].selected = False;
            awesomeconf->screens[screen].tags[i].was_selected = False;
            tmp = cfg_getstr(cfgsectmp, "layout");
            for(k = 0; k < awesomeconf->screens[screen].nlayouts; k++)
                if(awesomeconf->screens[screen].layouts[k].arrange == name_func_lookup(tmp, LayoutsList))
                    break;
            if(k == awesomeconf->screens[screen].nlayouts)
                k = 0;
            awesomeconf->screens[screen].tags[i].layout = &awesomeconf->screens[screen].layouts[k];
            awesomeconf->screens[screen].tags[i].mwfact = cfg_getfloat(cfgsectmp, "mwfact");
            awesomeconf->screens[screen].tags[i].nmaster = cfg_getint(cfgsectmp, "nmaster");
            awesomeconf->screens[screen].tags[i].ncol = cfg_getint(cfgsectmp, "ncol");
        }
	 
        if(!awesomeconf->screens[screen].ntags)
            eprint("awesome: fatal: no tags found in configuration file\n");

        /* select first tag by default */
        awesomeconf->screens[screen].tags[0].selected = True;
        awesomeconf->screens[screen].tags[0].was_selected = True;

        /* padding */
        awesomeconf->screens[screen].padding.top = cfg_getint(cfg_padding, "top");
        awesomeconf->screens[screen].padding.bottom = cfg_getint(cfg_padding, "bottom");
        awesomeconf->screens[screen].padding.left = cfg_getint(cfg_padding, "left");
        awesomeconf->screens[screen].padding.right = cfg_getint(cfg_padding, "right");
    }

    /* get general sections */
    cfg_rules = cfg_getsec(cfg, "rules");
    cfg_keys = cfg_getsec(cfg, "keys");
    cfg_mouse = cfg_getsec(cfg, "mouse");

    /* Rules */
    if(cfg_size(cfg_rules, "rule"))
    {
        awesomeconf->rules = rule = p_new(Rule, 1);
        for(j = 0; j < cfg_size(cfg_rules, "rule"); j++)
        {
            cfgsectmp = cfg_getnsec(cfg_rules, "rule", j);
            rule->prop = a_strdup(cfg_getstr(cfgsectmp, "name"));
            rule->tags = a_strdup(cfg_getstr(cfgsectmp, "tags"));
            if(!a_strlen(rule->tags))
                rule->tags = NULL;
            rule->isfloating = cfg_getbool(cfgsectmp, "float");
            rule->screen = cfg_getint(cfgsectmp, "screen");
            if(rule->screen >= get_screen_count(awesomeconf->display))
                rule->screen = 0;

            if(j < cfg_size(cfg_rules, "rule") - 1)
            {
                rule->next = p_new(Rule, 1);
                rule = rule->next;
            }
            else
                rule->next = NULL;
        }
    }
    else
        awesomeconf->rules = NULL;

    compileregs(awesomeconf->rules);


    /* Mouse: tags click bindings */
    awesomeconf->buttons.tag = parse_mouse_bindings(cfg_mouse, "tag", False);

    /* Mouse: layout click bindings */
    awesomeconf->buttons.layout = parse_mouse_bindings(cfg_mouse, "layout", True);

    /* Mouse: title click bindings */
    awesomeconf->buttons.title = parse_mouse_bindings(cfg_mouse, "title", True);

    /* Mouse: root window click bindings */
    awesomeconf->buttons.root = parse_mouse_bindings(cfg_mouse, "root", True);

    /* Mouse: client windows click bindings */
    awesomeconf->buttons.client = parse_mouse_bindings(cfg_mouse, "client", True);

    /* Keys */
    awesomeconf->numlockmask = get_numlockmask(awesomeconf->display);

    if(cfg_size(cfg_keys, "key"))
    {
        awesomeconf->keys = key = p_new(Key, 1);
        for(j = 0; j < cfg_size(cfg_keys, "key"); j++)
        {
            cfgsectmp = cfg_getnsec(cfg_keys, "key", j);
            for(l = 0; l < cfg_size(cfgsectmp, "modkey"); l++)
                key->mod |= key_mask_lookup(cfg_getnstr(cfgsectmp, "modkey", l));
            key->keysym = XStringToKeysym(cfg_getstr(cfgsectmp, "key"));
            key->func = name_func_lookup(cfg_getstr(cfgsectmp, "command"), UicbList);
            if(!key->func)
                fprintf(stderr, "awesome: unknown command %s\n", cfg_getstr(cfgsectmp, "command"));
            key->arg = a_strdup(cfg_getstr(cfgsectmp, "arg"));

            if(j < cfg_size(cfg_keys, "key") - 1)
            {
                key->next = p_new(Key, 1);
                key = key->next;
            }
            else
                key->next = NULL;
        }
    }
    else
        awesomeconf->keys = NULL;

    /* Free! Like a river! */
    cfg_free(cfg);
    p_delete(&confpath);
}

static unsigned int
get_numlockmask(Display *disp)
{
    XModifierKeymap *modmap;
    unsigned int mask = 0;
    int i, j;

    modmap = XGetModifierMapping(disp);
    for(i = 0; i < 8; i++)
        for(j = 0; j < modmap->max_keypermod; j++)
            if(modmap->modifiermap[i * modmap->max_keypermod + j]
               == XKeysymToKeycode(disp, XK_Num_Lock))
                mask = (1 << i);

    XFreeModifiermap(modmap);

    return mask;
}

/** Initialize color from X side
 * \param colorstr Color code
 * \param disp Display ref
 * \param scr Screen number
 * \return XColor pixel
 */
static XColor
initxcolor(Display *disp, int scr, const char *colstr)
{
    XColor color;
    if(!XAllocNamedColor(disp, DefaultColormap(disp, scr), colstr, &color, &color))
        die("awesome: error, cannot allocate color '%s'\n", colstr);
    return color;
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=99
