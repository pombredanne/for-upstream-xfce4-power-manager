/*
 * * Copyright (C) 2008-2011 Ali <aliov@xfce.org>
 *
 * Licensed under the GNU General Public License Version 2
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


/*
 * Based on code from gpm-button (gnome power manager)
 * Copyright (C) 2006-2007 Richard Hughes <richard@hughsie.com>
 * 
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/X.h>
#include <X11/XF86keysym.h>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include <glib.h>

#include <libxfce4util/libxfce4util.h>

#include "xfpm-button.h"
#include "xfpm-enum.h"
#include "xfpm-enum-types.h"
#include "xfpm-debug.h"

static void xfpm_button_finalize   (GObject *object);

#define XFPM_BUTTON_GET_PRIVATE(o) \
(G_TYPE_INSTANCE_GET_PRIVATE((o), XFPM_TYPE_BUTTON, XfpmButtonPrivate))

static struct
{
    XfpmButtonKey    key;
    guint            key_code;
} xfpm_key_map [NUMBER_OF_BUTTONS] = { {0, 0}, };

struct XfpmButtonPrivate
{
    GdkScreen	*screen;
    GdkWindow   *window;
    
    guint8       mapped_buttons;
};

enum
{
    BUTTON_PRESSED,
    LAST_SIGNAL
};

#define DUPLICATE_SHUTDOWN_TIMEOUT 4.0f

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE(XfpmButton, xfpm_button, G_TYPE_OBJECT)

static guint
xfpm_button_get_key (unsigned int keycode)
{
    XfpmButtonKey key = BUTTON_UNKNOWN;
    guint i;
    
    for ( i = 0; i < G_N_ELEMENTS (xfpm_key_map); i++)
    {
	if ( xfpm_key_map [i].key_code == keycode )
	    key = xfpm_key_map [i].key;
    }
    
    return key;
}

static GdkFilterReturn
xfpm_button_filter_x_events (GdkXEvent *xevent, GdkEvent *ev, gpointer data)
{
    XfpmButtonKey key;
    XfpmButton *button;
    
    XEvent *xev = (XEvent *) xevent;
    
    if ( xev->type != KeyPress )
    	return GDK_FILTER_CONTINUE;
    
    key = xfpm_button_get_key (xev->xkey.keycode);
    
    if ( key != BUTTON_UNKNOWN )
    {
	button = (XfpmButton *) data;
    
	XFPM_DEBUG_ENUM (key, XFPM_TYPE_BUTTON_KEY, "Key press");
    
	g_signal_emit (G_OBJECT(button), signals[BUTTON_PRESSED], 0, key);
	return GDK_FILTER_REMOVE;
    }
    
    return GDK_FILTER_CONTINUE;
}

static gboolean
xfpm_button_grab_keystring (XfpmButton *button, guint keycode)
{
    Display *display;
    guint ret;
    guint modmask = AnyModifier;
    
    display = GDK_DISPLAY ();
    
    gdk_error_trap_push ();

    ret = XGrabKey (display, keycode, modmask,
		    GDK_WINDOW_XID (button->priv->window), True,
		    GrabModeAsync, GrabModeAsync);
		    
    if ( ret == BadAccess )
    {
	g_warning ("Failed to grab modmask=%u, keycode=%li",
		    modmask, (long int) keycode);
	return FALSE;
    }
	
    ret = XGrabKey (display, keycode, LockMask | modmask,
		    GDK_WINDOW_XID (button->priv->window), True,
		    GrabModeAsync, GrabModeAsync);
			
    if (ret == BadAccess)
    {
	g_warning ("Failed to grab modmask=%u, keycode=%li",
		   LockMask | modmask, (long int) keycode);
	return FALSE;
    }

    gdk_flush ();
    gdk_error_trap_pop ();
    return TRUE;
}


static gboolean
xfpm_button_xevent_key (XfpmButton *button, guint keysym , XfpmButtonKey key)
{
    guint keycode = XKeysymToKeycode (GDK_DISPLAY(), keysym);

    if ( keycode == 0 )
    {
	g_warning ("could not map keysym %x to keycode\n", keysym);
	return FALSE;
    }
    
    if ( !xfpm_button_grab_keystring(button, keycode)) 
    {
    	g_warning ("Failed to grab %i\n", keycode);
	return FALSE;
    }
    
    XFPM_DEBUG_ENUM (key, XFPM_TYPE_BUTTON_KEY, "Grabbed key %li ", (long int) keycode);
    
    xfpm_key_map [key].key_code = keycode;
    xfpm_key_map [key].key = key;
    
    return TRUE;
}

static void
xfpm_button_setup (XfpmButton *button)
{
    button->priv->screen = gdk_screen_get_default ();
    button->priv->window = gdk_screen_get_root_window (button->priv->screen);
    
    if ( xfpm_button_xevent_key (button, XF86XK_PowerOff, BUTTON_POWER_OFF) )
	button->priv->mapped_buttons |= POWER_KEY;
    
#ifdef HAVE_XF86XK_HIBERNATE
    if ( xfpm_button_xevent_key (button, XF86XK_Hibernate, BUTTON_HIBERNATE) )
	button->priv->mapped_buttons |= HIBERNATE_KEY;
#endif 

#ifdef HAVE_XF86XK_SUSPEND
    if ( xfpm_button_xevent_key (button, XF86XK_Suspend, BUTTON_HIBERNATE) )
	button->priv->mapped_buttons |= HIBERNATE_KEY;
#endif 

    if ( xfpm_button_xevent_key (button, XF86XK_Sleep, BUTTON_SLEEP) )
	button->priv->mapped_buttons |= SLEEP_KEY;
	
    if ( xfpm_button_xevent_key (button, XF86XK_MonBrightnessUp, BUTTON_MON_BRIGHTNESS_UP) )
	button->priv->mapped_buttons |= BRIGHTNESS_KEY_UP;

    if (xfpm_button_xevent_key (button, XF86XK_MonBrightnessDown, BUTTON_MON_BRIGHTNESS_DOWN) )
	button->priv->mapped_buttons |= BRIGHTNESS_KEY_DOWN;
	
    xfpm_button_xevent_key (button, XF86XK_Battery, BUTTON_BATTERY);

    if ( xfpm_button_xevent_key (button, XF86XK_KbdBrightnessUp, BUTTON_KBD_BRIGHTNESS_UP) )
	button->priv->mapped_buttons |= KBD_BRIGHTNESS_KEY_UP;

    if (xfpm_button_xevent_key (button, XF86XK_KbdBrightnessDown, BUTTON_KBD_BRIGHTNESS_DOWN) )
	button->priv->mapped_buttons |= KBD_BRIGHTNESS_KEY_DOWN;

    gdk_window_add_filter (button->priv->window, 
			   xfpm_button_filter_x_events, button);
}

static void
xfpm_button_class_init(XfpmButtonClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    signals [BUTTON_PRESSED] = 
        g_signal_new ("button-pressed",
                      XFPM_TYPE_BUTTON,
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XfpmButtonClass, button_pressed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__ENUM,
                      G_TYPE_NONE, 1, XFPM_TYPE_BUTTON_KEY);

    object_class->finalize = xfpm_button_finalize;

    g_type_class_add_private (klass, sizeof (XfpmButtonPrivate));
}

static void
xfpm_button_init (XfpmButton *button)
{
    button->priv = XFPM_BUTTON_GET_PRIVATE (button);
    
    button->priv->mapped_buttons = 0;
    button->priv->screen = NULL;
    button->priv->window = NULL;
    
    xfpm_button_setup (button);
}

static void
xfpm_button_finalize (GObject *object)
{
    G_OBJECT_CLASS(xfpm_button_parent_class)->finalize(object);
}

XfpmButton *
xfpm_button_new (void)
{
    static gpointer xfpm_button_object = NULL;
    
    if ( G_LIKELY (xfpm_button_object != NULL) )
    {
        g_object_ref (xfpm_button_object);
    }
    else
    {
        xfpm_button_object = g_object_new (XFPM_TYPE_BUTTON, NULL);
        g_object_add_weak_pointer (xfpm_button_object, &xfpm_button_object);
    }
    
    return XFPM_BUTTON (xfpm_button_object);
}

guint8 xfpm_button_get_mapped (XfpmButton *button)
{
    g_return_val_if_fail (XFPM_IS_BUTTON (button), 0);
    
    return button->priv->mapped_buttons;
}
