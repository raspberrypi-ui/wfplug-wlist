/*============================================================================
Copyright (c) 2026 Raspberry Pi
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
============================================================================*/

#include <locale.h>
#include <glib/gi18n.h>
#include <gio/gdesktopappinfo.h>
#include <gdk/gdkwayland.h>
#include <menu-cache.h>

#include "lxutils.h"
#include "launcher.h"

#include "tlist.h"

/*----------------------------------------------------------------------------*/
/* Typedefs and macros                                                        */
/*----------------------------------------------------------------------------*/

#define HANDLE_PTR struct zwlr_foreign_toplevel_handle_v1 *
#define MANAGER_PTR struct zwlr_foreign_toplevel_manager_v1 *

#define MAX_MENU_LEN 25

#define STRCMP(a,b) (a && b && !g_strcmp0 (a, b))

/*----------------------------------------------------------------------------*/
/* Global data                                                                */
/*----------------------------------------------------------------------------*/

conf_table_t conf_table[2] = {
    {CONF_TYPE_INT,     "spacing",      N_("Item spacing"),     NULL,   "0" },
    {CONF_TYPE_NONE,    NULL,           NULL,                   NULL,   NULL}
};

/* The toplevel manager, its registry binding, and the list of tracked
 * windows all live for the lifetime of the process, not the lifetime of
 * any one widget instance - see the comment in wlist_init() for why
 * destroying/recreating them on every reload is unsafe. active_plugin
 * points at whichever WinlistPlugin instance is currently showing the UI,
 * or NULL in the brief window between one instance being destroyed and
 * the next being created during a reload. Buttons (WindowBtn) are cheap
 * to rebuild and stay per-instance, same as before. */
static struct wl_registry *global_registry = NULL;
static struct zwlr_foreign_toplevel_manager_v1 *global_manager = NULL;
static GList *global_windows = NULL;
static WinlistPlugin *active_plugin = NULL;
static MenuCache *global_menu_cache = NULL;

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static void handle_toplevel_title (void *, HANDLE_PTR handle, const char *title);
static void handle_toplevel_app_id (void *, HANDLE_PTR handle, const char *app_id);
static void handle_toplevel_parent (void *, HANDLE_PTR handle, HANDLE_PTR parent);
static void handle_toplevel_state (void *, HANDLE_PTR handle, struct wl_array *state);
static void handle_toplevel_done (void *, HANDLE_PTR handle);
static void handle_toplevel_closed (void *, HANDLE_PTR handle);
static void handle_toplevel_output_enter (void *, HANDLE_PTR, struct wl_output *);
static void handle_toplevel_output_leave (void *, HANDLE_PTR, struct wl_output *);
static void handle_manager_toplevel (void *, MANAGER_PTR, HANDLE_PTR toplevel);
static void handle_manager_finished (void *, MANAGER_PTR);
static void registry_add_object (void *, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void registry_remove_object (void *, struct wl_registry *, uint32_t);
static void activate_handle (GtkWidget *, gpointer userdata);
static gboolean activate_app (GtkWidget *, gpointer userdata);
static void close_app (GtkWidget *, gpointer userdata);
static void maximise_app (GtkWidget *, gpointer userdata);
static void unmaximise_app (GtkWidget *, gpointer userdata);
static void minimise_app (GtkWidget *, gpointer userdata);
static void unminimise_app (GtkWidget *, gpointer userdata);
static WindowBtn *find_btn (WinlistPlugin *wl, WindowItem *item);
static void create_button (WinlistPlugin *wl, WindowBtn *item);
static void create_or_update_button (WinlistPlugin *wl, WindowItem *item);
static void set_icon (WinlistPlugin *wl, WindowBtn *item);
static void destroy_button (gpointer data);
static gboolean update_button_states (WinlistPlugin *wl);
static float score_match (const char *str1, const char *str2);
static char *get_exe (const char *cmdline);
static char *menu_cache_id (WinlistPlugin *wl, const char *app_id);
static void popup_menu (GtkWidget *widget, gpointer userdata);
static void launch_id (WinlistPlugin *wl, GtkWidget *widget);
static char *get_string (char *cmd);
static char *find_alternative (const char *launch_id);
static void add_launcher (WinlistPlugin *wl, char *id);
static void load_launchers (WinlistPlugin *wl);
static void remove_launcher (GtkWidget *widget, gpointer);
static void destroy_toplevel_entry (gpointer data);
static void theme_changed (GtkWidget *, gpointer userdata);
static gboolean handle_button_pressed (GtkWidget *widget, GdkEventButton *event, gpointer userdata);
static gboolean handle_button_release (GtkWidget *widget, GdkEventButton *event, gpointer userdata);
static void handle_gesture_end (GtkGestureLongPress *, GdkEventSequence *, gpointer userdata);
static void handle_drag_begin (GtkGestureDrag *, gdouble, gdouble, gpointer userdata);
static void handle_drag_update (GtkGestureDrag *, gdouble, gdouble, gpointer userdata);
static void handle_drag_end (GtkGestureDrag *, gdouble, gdouble, gpointer userdata);

/*----------------------------------------------------------------------------*/
/* Wayland protocol interface                                                 */
/*----------------------------------------------------------------------------*/

static void handle_toplevel_title (void *, HANDLE_PTR handle, const char *title)
{
    WindowItem *item;
    GList *list;

    list = global_windows;
    while (list)
    {
        item = (WindowItem *) list->data;
        if (item->handle == (void *) handle)
        {
            if (!item->title) item->title = g_strdup (title);
            else
            {
                g_free (item->title);
                item->title = g_strdup (title);
            }
            break;
        }
        list = g_list_next (list);
    }
}

static void handle_toplevel_app_id (void *, HANDLE_PTR handle, const char *app_id)
{
    WindowItem *item;
    GList *list;

    list = global_windows;
    while (list)
    {
        item = (WindowItem *) list->data;
        if (item->handle == (void *) handle)
        {
            item->app_id = g_strdup (app_id);
            break;
        }
        list = g_list_next (list);
    }
}

static void handle_toplevel_parent (void *, HANDLE_PTR handle, HANDLE_PTR parent)
{
    WindowItem *item;
    GList *list;

    list = global_windows;
    while (list)
    {
        item = (WindowItem *) list->data;
        if (item->handle == (void *) handle)
        {
            item->parent = (void *) parent;
            break;
        }
        list = g_list_next (list);
    }
}

static void handle_toplevel_state (void *, HANDLE_PTR handle, struct wl_array *state)
{
    WindowItem *item;
    GList *list;
    int flags = 0;
    uint32_t *arr;

    wl_array_for_each (arr, state)
    {
        if (*arr == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED)
            flags |= STATE_ACTIVATED;

        if (*arr == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED)
            flags |= STATE_MAXIMISED;

        if (*arr == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED)
            flags |= STATE_MINIMISED;
    }

    list = global_windows;
    while (list)
    {
        item = (WindowItem *) list->data;
        if (item->handle == (void *) handle)
        {
            item->state = flags;
            break;
        }
        list = g_list_next (list);
    }
}

static void handle_toplevel_done (void *, HANDLE_PTR handle)
{
    WindowItem *item;
    GList *list;

    list = global_windows;
    while (list)
    {
        item = (WindowItem *) list->data;
        if (item->handle == (void *) handle)
        {
            if (item->title && item->app_id && !item->parent && active_plugin)
            {
                if (!item->button_created)
                {
                    // new toplevel - update launcher or create a new button
                    item->button_created = TRUE;
                    create_or_update_button (active_plugin, item);
                }
            }
            if (item->state & STATE_ACTIVATED && list != global_windows)
            {
                // move this item to the front of the list if it is activated
                global_windows = g_list_remove_link (global_windows, list);
                global_windows = g_list_concat (list, global_windows);
            }
            break;
        }
        list = g_list_next (list);
    }
    if (active_plugin) update_button_states (active_plugin);
}

static void handle_toplevel_closed (void *, HANDLE_PTR handle)
{
    WindowItem *item;
    WindowBtn *btn = NULL;
    GList *list;

    list = global_windows;
    while (list)
    {
        item = (WindowItem *) list->data;
        if (item->handle == (void *) handle)
        {
            if (!item->parent && active_plugin) btn = find_btn (active_plugin, item);

            // safe to destroy now - the compositor has told us it's gone,
            // so there's no risk of a late .toplevel() event for it being
            // discarded with an unreserved new_id (see wlist_init())
            zwlr_foreign_toplevel_handle_v1_destroy ((HANDLE_PTR) item->handle);
            destroy_toplevel_entry (item);
            global_windows = g_list_delete_link (global_windows, list);

            if (btn)
            {
                btn->windows--;
                if (!btn->windows && !btn->launcher)
                {
                    // not a launcher and no open windows - remove button
                    destroy_button (btn);
                    active_plugin->buttons = g_list_delete_link (active_plugin->buttons, g_list_find (active_plugin->buttons, btn));
                }
                else
                {
                    // update the window count on the icon
                    set_icon (active_plugin, btn);
                }
            }
            break;
        }
        list = g_list_next (list);
    }
    if (active_plugin) update_button_states (active_plugin);
}

static void handle_toplevel_output_enter (void *, HANDLE_PTR, struct wl_output *)
{
}

static void handle_toplevel_output_leave (void *, HANDLE_PTR, struct wl_output *)
{
}

struct zwlr_foreign_toplevel_handle_v1_listener toplevel_handle_v1 = 
{
    .title  = handle_toplevel_title,
    .app_id = handle_toplevel_app_id,
    .parent = handle_toplevel_parent,
    .state  = handle_toplevel_state,
    .done   = handle_toplevel_done,
    .closed = handle_toplevel_closed,
    .output_enter = handle_toplevel_output_enter,
    .output_leave = handle_toplevel_output_leave
};

static void handle_manager_toplevel (void *, MANAGER_PTR, HANDLE_PTR toplevel)
{
    WindowItem *item = g_new0 (WindowItem, 1);

    item->handle = (void *) toplevel;
    global_windows = g_list_prepend (global_windows, item);
    zwlr_foreign_toplevel_handle_v1_add_listener (toplevel, &toplevel_handle_v1, NULL);
}

static void handle_manager_finished (void *, MANAGER_PTR)
{
    global_manager = NULL;
}

struct zwlr_foreign_toplevel_manager_v1_listener toplevel_manager_v1 =
{
    .toplevel = handle_manager_toplevel,
    .finished = handle_manager_finished,
};

static void registry_add_object (void *, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    if (!g_strcmp0 (interface, zwlr_foreign_toplevel_manager_v1_interface.name) && !global_manager)
    {
        global_manager = (MANAGER_PTR) wl_registry_bind (registry, name, &zwlr_foreign_toplevel_manager_v1_interface, version < 3 ? version : 3);
        zwlr_foreign_toplevel_manager_v1_add_listener (global_manager, &toplevel_manager_v1, NULL);
    }
}

static void registry_remove_object (void *, struct wl_registry *, uint32_t)
{
}

static struct wl_registry_listener registry_listener =
{
    &registry_add_object,
    &registry_remove_object
};

/*----------------------------------------------------------------------------*/
/* Window handle controls                                                     */
/*----------------------------------------------------------------------------*/

static void activate_handle (GtkWidget *, gpointer userdata)
{
    GdkDisplay *gdk_display = gdk_display_get_default ();
    GdkSeat *seat = gdk_display_get_default_seat (gdk_display);
    struct wl_seat *wseat  = gdk_wayland_seat_get_wl_seat (seat);

    zwlr_foreign_toplevel_handle_v1_activate ((HANDLE_PTR) userdata, wseat);
}

static gboolean activate_app (GtkWidget *wid, gpointer)
{
    gboolean min = FALSE, act = FALSE;
    GList *list, *new, *prev;
    WindowItem *item;
    const char *id = gtk_widget_get_name (wid);
    int contig = -1;    // flag used to detect contiguity of windows - should be 1 if they are all together at the front

    list = g_list_last (global_windows);
    while (list)
    {
        item = (WindowItem *) list->data;
        if (STRCMP (item->app_id, id))
        {
            // if any window is minimised, activate
            // if all windows are unminimised but none are activated, activate
            // if all windows are visible, one is activated, but windows are not contiguous, activate
            if (item->state & STATE_MINIMISED) min = TRUE;  // at least one window minimised
            if (item->state & STATE_ACTIVATED) act = TRUE;  // one window is activated
            if (contig == -1) contig = 1;
        }
        else if (contig == 1) contig = 0;
        list = list->prev;
    }

    if (!min && act && contig) return FALSE;

    list = g_list_last (global_windows);
    new = NULL;
    while (list)
    {
        prev = g_list_previous (list);
        item = (WindowItem *) list->data;
        if (STRCMP (item->app_id, id))
        {
            activate_handle (NULL, item->handle);

            // when an item is activated, move it to the front of a new list...
            global_windows = g_list_remove_link (global_windows, list);
            new = g_list_concat (list, new);
        }
        list = prev;
    }

    // ...and then concatenate the new list onto the remainder - keeps the list in stack order
    global_windows = g_list_concat (new, global_windows);

    return TRUE;
}

static void close_app (GtkWidget *wid, gpointer)
{
    GList *list = global_windows;
    const char *id = gtk_widget_get_name (wid);

    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (STRCMP (item->app_id, id)) zwlr_foreign_toplevel_handle_v1_close (item->handle);
        list = g_list_next (list);
    }
}

static void maximise_app (GtkWidget *wid, gpointer)
{
    GList *list = global_windows;
    const char *id = gtk_widget_get_name (wid);

    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (STRCMP (item->app_id, id))
        {
            zwlr_foreign_toplevel_handle_v1_unset_minimized (item->handle);
            zwlr_foreign_toplevel_handle_v1_set_maximized (item->handle);
        }
        list = g_list_next (list);
    }
}

static void unmaximise_app (GtkWidget *wid, gpointer)
{
    GList *list = global_windows;
    const char *id = gtk_widget_get_name (wid);

    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (STRCMP (item->app_id, id))
        {
            zwlr_foreign_toplevel_handle_v1_unset_minimized (item->handle);
            zwlr_foreign_toplevel_handle_v1_unset_maximized (item->handle);
        }
        list = g_list_next (list);
    }
}

static void minimise_app (GtkWidget *wid, gpointer)
{
    GList *list = global_windows;
    const char *id = gtk_widget_get_name (wid);

    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (STRCMP (item->app_id, id)) zwlr_foreign_toplevel_handle_v1_set_minimized (item->handle);
        list = g_list_next (list);
    }
}

static void unminimise_app (GtkWidget *wid, gpointer)
{
    GList *list = global_windows;
    const char *id = gtk_widget_get_name (wid);

    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (STRCMP (item->app_id, id)) zwlr_foreign_toplevel_handle_v1_unset_minimized (item->handle);
        list = g_list_next (list);
    }
}

/*----------------------------------------------------------------------------*/
/* Button management                                                          */
/*----------------------------------------------------------------------------*/

static WindowBtn *find_btn (WinlistPlugin *wl, WindowItem *item)
{
    GList *btns = wl->buttons;
    char *mcid = menu_cache_id (wl, item->app_id);
    while (btns)
    {
        WindowBtn *btn = (WindowBtn *) btns->data;
        if (STRCMP (mcid, btn->launch_id) || STRCMP (mcid, btn->alt_launch_id) || STRCMP (item->app_id, btn->app_id))
        {
            g_free (mcid);
            return btn;
        }
        btns = g_list_next (btns);
    }
    g_free (mcid);
    return NULL;
}

static void create_button (WinlistPlugin *wl, WindowBtn *item)
{
    item->btn = gtk_toggle_button_new ();

    g_signal_connect (item->btn, "button-press-event", G_CALLBACK (handle_button_pressed), wl);
    g_signal_connect (item->btn, "button-release-event", G_CALLBACK (handle_button_release), wl);

    item->gesture = add_long_press (item->btn, G_CALLBACK (handle_gesture_end), item);

    item->dgesture = gtk_gesture_drag_new (item->btn);
    g_signal_connect (item->dgesture, "drag-begin", G_CALLBACK (handle_drag_begin), wl);
    g_signal_connect (item->dgesture, "drag-update", G_CALLBACK (handle_drag_update), wl);
    g_signal_connect (item->dgesture, "drag-end", G_CALLBACK (handle_drag_end), wl);

    gtk_container_add (GTK_CONTAINER (wl->box), item->btn);
    item->icon = NULL;
    set_icon (wl, item);
    gtk_widget_show_all (wl->plugin);
}

static void create_or_update_button (WinlistPlugin *wl, WindowItem *item)
{
    WindowBtn *btn;
    btn = find_btn (wl, item);
    if (btn)
    {
        // found a button already for this app_id - update with new title, state etc
        btn->windows++;
        set_icon (wl, btn);
        btn->app_id = g_strdup (item->app_id);
        gtk_widget_set_name (btn->btn, item->app_id);
    }
    else
    {
        // ...and if not, create one
        btn = g_new0 (WindowBtn, 1);
        btn->app_id = g_strdup (item->app_id);
        btn->launch_id = NULL;
        btn->alt_launch_id = NULL;
        btn->windows = 1;
        btn->plugin = wl;
        btn->launcher = FALSE;
        create_button (wl, btn);
        gtk_widget_set_name (btn->btn, item->app_id);
        gtk_widget_set_tooltip_text (btn->btn, btn->tooltip);
        wl->buttons = g_list_prepend (wl->buttons, btn);
    }
}

static void set_icon (WinlistPlugin *wl, WindowBtn *item)
{
    char *str, *id, *buf;
    GAppInfo *info;
    GIcon *ic;
    GtkStyleContext *sc;
    GdkRGBA col;
    MenuCacheItem *mitem;
    int fsize, dimx, dimy, radius;
    GdkPixbuf *pb;
    cairo_surface_t *surf;
    cairo_t *cr;

    // delete any existing icon
    if (item->icon) gtk_widget_destroy (item->icon);

    // create the desktop file name from the app_id
    str = g_strdup_printf ("%s.desktop", item->launch_id ? item->launch_id : item->app_id);
    info = (GAppInfo *) g_desktop_app_info_new (str);
    g_free (str);

    if (info)
    {
        // the desktop file name is valid, so just get the icon from it
        ic = g_app_info_get_icon (info);
        str = g_icon_to_string (ic);
        item->tooltip = g_strdup (g_app_info_get_name (info));
        g_object_unref (info);
    }
    else
    {
        // the desktop file name isn't valid, so search the menu cache for something similar
        str = NULL;
        id = menu_cache_id (wl, item->app_id);
        if (id)
        {
            str = g_strdup_printf ("%s.desktop", id);
            g_free (id);
            mitem = menu_cache_find_item_by_id (wl->menu_cache, str);
            g_free (str);
            str = NULL;

            if (mitem)
            {
                str = g_strdup (menu_cache_item_get_icon (mitem));
                item->tooltip = g_strdup (menu_cache_item_get_name (mitem));
                menu_cache_item_unref (mitem);
            }
        }
    }

    item->icon = gtk_image_new ();
    gtk_container_add (GTK_CONTAINER (item->btn), item->icon);

    pb = load_taskbar_pixbuf (item->icon, str);
    surf = gdk_cairo_surface_create_from_pixbuf (pb, 0, gtk_widget_get_window (wl->box));
    cr = cairo_create (surf);

    if (item->windows)
    {
        sc = gtk_widget_get_style_context (wl->plugin);
        dimx = gdk_pixbuf_get_width (pb) / gtk_widget_get_scale_factor (item->btn);
        dimy = gdk_pixbuf_get_height (pb) / gtk_widget_get_scale_factor (item->btn);
        radius = dimy / 6;
        if (dimy == 96) fsize = 21;
        if (dimy == 64) fsize = 14;
        if (dimy == 48) fsize = 11;
        if (dimy == 32) fsize = 7;
        if (dimy == 24) fsize = 5;
        if (dimy == 16) fsize = 3;

        gtk_style_context_get_color (sc, GTK_STATE_FLAG_NORMAL, &col);
        cairo_set_source_rgb (cr, 1 - col.red, 1 - col.green, 1 - col.blue);
        cairo_arc (cr, dimx - radius, dimy - radius, radius, 0, 6.3);
        cairo_fill (cr);

        if (item->windows < 10) buf = g_strdup_printf ("%d", item->windows);
        else buf = g_strdup ("*");

        cairo_set_source_rgb (cr, col.red, col.green, col.blue);
        cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size (cr, fsize);
        cairo_move_to (cr, dimx - fsize, dimy - (dimy / 12));
        cairo_show_text (cr, buf);
        g_free (buf);
    }

    gtk_image_set_from_surface (GTK_IMAGE (item->icon), surf);
    cairo_surface_destroy (surf);

    gtk_widget_show_all (item->btn);

    g_free (str);
}

static void destroy_button (gpointer data)
{
    WindowBtn *item = (WindowBtn *) data;
    if (item->app_id) g_free (item->app_id);
    if (item->launch_id) g_free (item->launch_id);
    if (item->alt_launch_id) g_free (item->alt_launch_id);
    if (item->icon) gtk_widget_destroy (item->icon);
    if (item->btn) gtk_widget_destroy (item->btn);
    if (item->gesture) g_object_unref (item->gesture);
    if (item->dgesture) g_object_unref (item->dgesture);
    item->app_id = NULL;
    item->launch_id = NULL;
    item->alt_launch_id = NULL;
    item->icon = NULL;
    item->btn = NULL;
    item->gesture = NULL;
    item->dgesture = NULL;
}

static gboolean update_button_states (WinlistPlugin *wl)
{
    GList *btns, *list;
    gboolean active;
    WindowBtn *btn;
    WindowItem *item;
    const char *id;

    btns = wl->buttons;
    while (btns)
    {
        btn = (WindowBtn *) btns->data;
        id = gtk_widget_get_name (btn->btn);
        active = FALSE;
        list = global_windows;
        while (list)
        {
            item = (WindowItem *) list->data;
            if (STRCMP (item->app_id, id) && item->state & STATE_ACTIVATED) active = TRUE;
            list = g_list_next (list);
        }

        g_signal_handlers_block_by_func (btn->btn, G_CALLBACK (handle_button_pressed), btn->plugin);
        g_signal_handlers_block_by_func (btn->btn, G_CALLBACK (handle_button_release), btn->plugin);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn->btn), active);
        g_signal_handlers_unblock_by_func (btn->btn, G_CALLBACK (handle_button_pressed), btn->plugin);
        g_signal_handlers_unblock_by_func (btn->btn, G_CALLBACK (handle_button_release), btn->plugin);

        btns = g_list_next (btns);
    }
    return FALSE;
}

/*----------------------------------------------------------------------------*/
/* Menu cache search                                                          */
/*----------------------------------------------------------------------------*/

/* This is an attempt to score how similar two strings are by comparing how many letters
 * at the start of each are identical, and how many letters at the end are identical.
 * It's not perfect... */

static float score_match (const char *str1, const char *str2)
{
    int score, pos1, pos2;
    char *str1l, *str2l;
    float result;

    if (!str1 || !str2) return 0.0;

    str1l = g_ascii_strdown (str1, -1);
    str2l = g_ascii_strdown (str2, -1);
    score = 0;

    // count matching characters from start
    pos1 = 0;
    while (str1l[pos1] && str2l[pos1] && str1l[pos1] == str2l[pos1])
    {
        score++;
        pos1++;
    }

    // count matching characters from end
    pos1 = strlen (str1l) - 1;
    pos2 = strlen (str2l) - 1;
    while (pos1 && pos2 && str1l[pos1] == str2l[pos2])
    {
        score++;
        pos1--;
        pos2--;
    }

    result = score;
    if (strlen (str1l) > strlen (str2l)) result /= strlen (str2l);
    else result /= strlen (str1l);

    g_free (str1l);
    g_free (str2l);

    return result;
}

static char *get_exe (const char *cmdline)
{
    // g_path_get_basename fails with quoted paths, so...
    char *buf, *start, *end, *ret;
    char del;

    buf = g_strdup (cmdline);

    start = buf;
    if (strchr ("'\"", *start))
    {
        del = *start;
        start++;
    }
    else del = ' ';

    end = start;
    while (*end)
    {
        if (*end == del) break;
        end++;
    }
    *end = 0;

    if (strrchr (start, '/')) start = strrchr (start, '/') + 1;
    ret = g_strdup (start);
    g_free (buf);

    return ret;
}

static char *menu_cache_id (WinlistPlugin *wl, const char *app_id)
{
    // DIAGNOSTIC: menu_cache lookup is disabled below in widget_init(), so
    // wl->menu_cache is NULL - guard against dereferencing it.
    if (!wl->menu_cache) return NULL;

    MenuCacheItem *item;
    GSList *list, *iter;
    GAppInfo *info;
    char *id, *exec, *s2, *best = NULL;
    float res, score;
    const char *ex, *s1;

    // loop through the cache to find the best match
    score = 0.0;
    list = menu_cache_list_all_apps (wl->menu_cache);
    iter = list;
    while (iter)
    {
        item = (MenuCacheItem *) iter->data;

        // first check that the cache item is a valid desktop info, i.e. has an associated exec
        id = g_strdup (menu_cache_item_get_id (item));
        info = (GAppInfo *) g_desktop_app_info_new (id);
        if (!info)
        {
            g_free (id);
            iter = iter->next;
            continue;
        }
        else g_object_unref (info);

        // strip the .desktop from the end for matching purposes
        *strrchr (id, '.') = 0;

        // if there is a caseless match with the app-id, this is correct - return it
        if (!g_ascii_strncasecmp (app_id, id, 1000))
        {
            g_slist_free_full (list, (GDestroyNotify) ((void *) menu_cache_item_unref));
            return id;
        }

        // try matching the part of the id after a final .
        s1 = strrchr (app_id, '.') ? strrchr (app_id, '.') + 1 : app_id;
        s2 = strrchr (id, '.') ? strrchr (id, '.') + 1 : id;
        if (!g_ascii_strncasecmp (s1, s2, 1000))
        {
            g_slist_free_full (list, (GDestroyNotify) ((void *) menu_cache_item_unref));
            return id;
        }

        iter = iter->next;
    }

    // no joy - try matching executable names
    iter = list;
    while (iter)
    {
        item = (MenuCacheItem *) iter->data;

        // first check that the cache item is a valid desktop info, i.e. has an associated exec
        id = g_strdup (menu_cache_item_get_id (item));
        info = (GAppInfo *) g_desktop_app_info_new (id);
        if (!info)
        {
            g_free (id);
            iter = iter->next;
            continue;
        }
        else g_object_unref (info);

        // strip the .desktop from the end for matching purposes
        *strrchr (id, '.') = 0;

        // get the executable name
        ex = menu_cache_app_get_exec ((MenuCacheApp *) item);
        if (ex) exec = get_exe (ex);
        else exec = NULL;

        // if there is a caseless match with the executable, this is correct - return it
        if (exec && !g_ascii_strncasecmp (app_id, exec, 1000))
        {
            g_free (exec);
            if (best) g_free (best);
            g_slist_free_full (list, (GDestroyNotify) ((void *) menu_cache_item_unref));
            return id;
        }

        // look for matching characters at start and end
        res = score_match (app_id, id);
        if (res > score)
        {
            score = res;
            if (best) g_free (best);
            best = g_strdup (id);
        }

        if (exec)
        {
            res = score_match (app_id, exec);
            if (res > score)
            {
                score = res;
                if (best) g_free (best);
                best = g_strdup (id);
            }
            g_free (exec);
        }

        g_free (id);
        iter = iter->next;
    }
    g_slist_free_full (list, (GDestroyNotify) ((void *) menu_cache_item_unref));
    return best;
}

/*----------------------------------------------------------------------------*/
/* Right-click menu                                                           */
/*----------------------------------------------------------------------------*/

static void popup_menu (GtkWidget *widget, gpointer userdata)
{
    GtkWidget *menu, *item, *label;
    WindowBtn *btn = (WindowBtn *) userdata;
    WinlistPlugin *wl = btn->plugin;
    const char *id = gtk_widget_get_name (widget);
    WindowItem *app;
    GList *list;
    char *str, *esc;
    gboolean min = FALSE, max = FALSE, unmin = FALSE, unmax = FALSE;

    menu = gtk_menu_new ();

    list = global_windows;
    while (list)
    {
        app = (WindowItem *) list->data;
        if (STRCMP (app->app_id, id))
        {
            esc = g_markup_escape_text (app->title, -1);
            if (strlen (esc) <= MAX_MENU_LEN)
                item = gtk_menu_item_new_with_label (esc);
            else
            {
                str = g_strndup (esc, MAX_MENU_LEN);
                sprintf (str + MAX_MENU_LEN - 3, "...");
                item = gtk_menu_item_new_with_label (str);
                g_free (str);
            }
            g_free (esc);

            if (app->state & STATE_MINIMISED)
            {
                unmin = TRUE;
                label = gtk_bin_get_child (GTK_BIN (item));
                str = g_strdup_printf ("<i>%s</i>", gtk_label_get_text (GTK_LABEL (label)));
                gtk_label_set_markup (GTK_LABEL (label), str);
                g_free (str);
            }
            else min = TRUE;

            if (app->state & STATE_MAXIMISED)
            {
                unmax = TRUE;
                label = gtk_bin_get_child (GTK_BIN (item));
                str = g_strdup_printf ("<b>%s</b>", gtk_label_get_label (GTK_LABEL (label)));
                gtk_label_set_markup (GTK_LABEL (label), str);
                g_free (str);
            }
            else max = TRUE;

            g_signal_connect (item, "activate", G_CALLBACK (activate_handle), (void *) app->handle);
            gtk_widget_set_tooltip_text (item, app->title);
            gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
        }
        list = g_list_next (list);
    }

    if (btn->windows)
    {
        item = gtk_separator_menu_item_new ();
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    }

    if (min)
    {
        item = gtk_menu_item_new_with_label (btn->windows > 1 ? _("Hide All") : _("Hide"));
        gtk_widget_set_name (item, btn->app_id);
        g_signal_connect (item, "activate", G_CALLBACK (minimise_app), wl);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    }

    if (unmin)
    {
        item = gtk_menu_item_new_with_label (btn->windows > 1 ? _("Show All") : _("Show"));
        gtk_widget_set_name (item, btn->app_id);
        g_signal_connect (item, "activate", G_CALLBACK (unminimise_app), wl);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    }

    if (btn->windows == 1)
    {
        if (max)
        {
            item = gtk_menu_item_new_with_label (_("Maximise"));
            gtk_widget_set_name (item, btn->app_id);
            g_signal_connect (item, "activate", G_CALLBACK (maximise_app), wl);
            gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
        }

        if (unmax)
        {
            item = gtk_menu_item_new_with_label (_("Unmaximise"));
            gtk_widget_set_name (item, btn->app_id);
            g_signal_connect (item, "activate", G_CALLBACK (unmaximise_app), wl);
            gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
        }
    }

    if (btn->windows)
    {
        item = gtk_separator_menu_item_new ();
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

        item = gtk_menu_item_new_with_label (btn->windows > 1 ? _("Close All") : _("Close"));
        gtk_widget_set_name (item, btn->app_id);
        g_signal_connect (item, "activate", G_CALLBACK (close_app), wl);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

    }
    else if (btn->launch_id)
    {
        item = gtk_menu_item_new_with_label (_("Remove from Launcher"));
        gtk_widget_set_name (item, btn->launch_id);
        g_signal_connect (item, "activate", G_CALLBACK (remove_launcher), NULL);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    }

    gtk_widget_show_all (menu);
    wrap_show_menu (widget, menu);
}

/*----------------------------------------------------------------------------*/
/* Launchers                                                                  */
/*----------------------------------------------------------------------------*/

static void launch_id (WinlistPlugin *wl, GtkWidget *widget)
{
    char *lid, *str;
    GAppInfo *info;

    lid = menu_cache_id (wl, gtk_widget_get_name (widget));
    if (!lid) return;
    str = g_strdup_printf ("%s.desktop", lid);
    info = (GAppInfo *) g_desktop_app_info_new (str);

    g_app_info_launch (info, NULL, NULL, NULL);

    g_object_unref (info);
    g_free (lid);
    g_free (str);
}

static char *get_string (char *cmd)
{
    char *line = NULL, *res = NULL;
    size_t len = 0;
    FILE *fp = popen (cmd, "r");

    if (fp == NULL) return NULL;
    if (getline (&line, &len, fp) > 0)
    {
        res = line;
        while (*res)
        {
            if (g_ascii_isspace (*res)) *res = 0;
            res++;
        }
        res = g_strdup (line);
    }
    pclose (fp);
    g_free (line);
    return res;
}

static char *find_alternative (const char *launch_id)
{
    char *cmd = g_strdup_printf ("update-alternatives --query %s 2> /dev/null| grep Value | cut -d ' ' -f 2 | rev | cut -d / -f 1 | rev", launch_id);
    return get_string (cmd);
}

static void add_launcher (WinlistPlugin *wl, char *id)
{
    WindowBtn *wbtn;
    char *str;
    GAppInfo *info;

    wbtn = g_new0 (WindowBtn, 1);
    wbtn->launch_id = g_strdup (id);
    wbtn->alt_launch_id = find_alternative (id);
    wbtn->app_id = NULL;
    wbtn->windows = 0;
    wbtn->plugin = wl;
    wbtn->launcher = TRUE;
    create_button (wl, wbtn);
    gtk_widget_set_name (wbtn->btn, id);

    str = g_strdup_printf ("%s.desktop", id);
    info = (GAppInfo *) g_desktop_app_info_new (str);
    g_free (str);
    wbtn->tooltip = g_strdup (g_app_info_get_name (info));
    g_object_unref (info);

    gtk_widget_set_tooltip_text (wbtn->btn, wbtn->tooltip);
    wl->buttons = g_list_prepend (wl->buttons, wbtn);
}

static void load_launchers (WinlistPlugin *wl)
{
    char *lstr, *launcher;

    lstr = g_strdup (wl->launchers);
    launcher = strtok (lstr, " ");
    while (launcher)
    {
        add_launcher (wl, launcher);
        launcher = strtok (NULL, " ");
    }
    g_free (lstr);
}

static void remove_launcher (GtkWidget *widget, gpointer)
{
    remove_from_launcher (gtk_widget_get_name (widget));
}

/*----------------------------------------------------------------------------*/
/* Misc                                                                       */
/*----------------------------------------------------------------------------*/

static void destroy_toplevel_entry (gpointer data)
{
    WindowItem *item = (WindowItem *) data;
    if (item->title) g_free (item->title);
    if (item->app_id) g_free (item->app_id);
}

static void theme_changed (GtkWidget *, gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    wlist_update_display (wl);
}

/*----------------------------------------------------------------------------*/
/* Handlers                                                                   */
/*----------------------------------------------------------------------------*/

static gboolean handle_button_pressed (GtkWidget *wid, GdkEventButton *, gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    wl->dragbtn = wid;
    pressed = PRESS_NONE;
    return FALSE;
}

static gboolean handle_button_release (GtkWidget *wid, GdkEventButton *event, gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    WindowBtn *btn = NULL;

    GList *list = wl->buttons;
    while (list)
    {
        btn = (WindowBtn *) list->data;
        if (btn->btn == wid) break;
        list = g_list_next (list);
    }

    if (wl->dragon)
    {
        g_idle_add ((GSourceFunc) update_button_states, wl);
        return FALSE;
    }

    if (pressed == PRESS_LONG) return FALSE;

    switch (event->button)
    {
        case 1:     if (!btn->windows || !activate_app (wid, userdata)) launch_id (wl, wid);
                    return FALSE;

        case 3:     popup_menu (wid, btn);
                    return TRUE;
    }

    return FALSE;
}

static void handle_gesture_end (GtkGestureLongPress *, GdkEventSequence *, gpointer userdata)
{
    WindowBtn *btn = (WindowBtn *) userdata;

    if (btn->plugin->dragon) return;

    if (pressed == PRESS_LONG) popup_menu (btn->btn, btn);
}

static void handle_drag_begin (GtkGestureDrag *, gdouble x, gdouble, gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    wl->drag_start = x;
}

static void handle_drag_update (GtkGestureDrag *, gdouble x, gdouble, gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    GtkStyleContext *sc;
    GList *children, *index;
    int moveby, width;

    if (!wl->dragon && x < DRAG_THRESH && x > -DRAG_THRESH) return;

    wl->dragon = TRUE;
    pressed = PRESS_NONE;
    gdk_window_set_cursor (gtk_widget_get_window (wl->plugin), wl->drag);
    sc = gtk_widget_get_style_context (wl->dragbtn);
    gtk_style_context_add_class (sc, "drag");

    width = get_icon_size (wl->plugin);

    moveby = 0;
    if (wl->drag_start + x < -DRAG_THRESH) moveby = -1;
    if (wl->drag_start + x > width + DRAG_THRESH) moveby = 1;
    if (!moveby) return;

    children = gtk_container_get_children (GTK_CONTAINER (wl->box));
    index = children;
    while (index)
    {
        if (index->data == wl->dragbtn) break;
        moveby++;
        index = g_list_next (index);
    }
    g_list_free (children);

    if (moveby >= 0) gtk_box_reorder_child (GTK_BOX (wl->box), wl->dragbtn, moveby);

    gtk_widget_queue_allocate (wl->plugin);
}

static void handle_drag_end (GtkGestureDrag *, gdouble, gdouble, gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    GtkStyleContext *sc;
    GList *children, *index, *btns;
    char *launchers = NULL, *tmp;
    GtkWidget *btn;
    WindowBtn *b;

    if (!wl->dragon) return;

    children = gtk_container_get_children (GTK_CONTAINER (wl->box));
    index = children;
    while (index)
    {
        btn = GTK_WIDGET (index->data);
        btns = wl->buttons;
        while (btns)
        {
            b = (WindowBtn *) btns->data;
            if (b->btn == btn && b->launch_id)
            {
                tmp = g_strdup_printf ("%s%s ", launchers ? launchers : "", b->launch_id);
                g_free (launchers);
                launchers = tmp;
                break;
            }
            btns = g_list_next (btns);
        }
        index = g_list_next (index);
    }
    g_list_free (children);

    replace_launchers (launchers);
    g_free (launchers);

    wl->dragon = FALSE;
    gdk_window_set_cursor (gtk_widget_get_window (wl->plugin), NULL);
    sc = gtk_widget_get_style_context (wl->dragbtn);
    gtk_style_context_remove_class (sc, "drag");
}

/*----------------------------------------------------------------------------*/
/* wf-panel plugin functions                                                  */
/*----------------------------------------------------------------------------*/

/* Handler for system config changed message from panel */
void wlist_update_display (WinlistPlugin *wl)
{
    GList *list;
    WindowItem *item;

    // delete the existing buttons and free data
    g_list_free_full (wl->buttons, destroy_button);
    wl->buttons = NULL;

    // first load the launchers
    load_launchers (wl);

    // then go through the list of open windows, adding icons to launchers or adding new icons accordingly
    list = global_windows;
    while (list)
    {
        item = (WindowItem *) list->data;
        if (!item->parent) create_or_update_button (wl, item);
        list = g_list_next (list);
    }
    update_button_states (wl);

    gtk_box_set_spacing (GTK_BOX (wl->box), wl->spacing);
    gtk_widget_queue_allocate (wl->plugin);
}

void wlist_set_values (WinlistPlugin *wl)
{
    conf_table[0].value = (void *) &wl->spacing;
}

/* Handler for control message */
gboolean wlist_control_msg (WinlistPlugin *, const char *)
{
    return FALSE;
}

void wlist_init (WinlistPlugin *wl)
{
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

    /* Set up variables */
    wl->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, wl->spacing);
    gtk_box_set_homogeneous (GTK_BOX (wl->box), TRUE);
    gtk_box_set_spacing (GTK_BOX (wl->box), wl->spacing);
    gtk_container_add (GTK_CONTAINER (wl->plugin), wl->box);
    gtk_widget_realize (wl->box);
    wl->drag = gdk_cursor_new_for_display (gdk_display_get_default (), GDK_HAND1);

    wl->dragon = FALSE;
    wl->buttons = NULL;

    // menu_cache_lookup() returns the same process-wide, refcounted cache
    // object every time it's called for the same name - looking it up
    // again on every reload just leaks a ref and a reload-notify callback
    // (unreffing it is known to segfault because its io thread isn't
    // closed down, so it's never released), so only do it once for the
    // life of the process.
    if (!global_menu_cache)
    {
        gboolean need_prefix = (g_getenv ("XDG_MENU_PREFIX") == NULL);
        global_menu_cache = menu_cache_lookup (need_prefix ? "lxde-applications.menu+hidden" : "applications.menu+hidden");
        menu_cache_add_reload_notify (global_menu_cache, NULL, NULL);
    }
    wl->menu_cache = global_menu_cache;

    active_plugin = wl;

    // The toplevel manager (and the registry binding that finds it) are
    // bound once for the life of the process, not per widget instance.
    // Destroying and recreating zwlr_foreign_toplevel_manager_v1 (and its
    // handles) on every reload is unsafe: if the compositor sends one more
    // .toplevel() event for a manager we've just destroyed - e.g. because
    // a new window opens in that exact instant - libwayland silently
    // discards the whole event, since the proxy is already a zombie. That
    // discard includes the new_id the event carries, which never gets
    // reserved in the client's object map. The compositor doesn't know or
    // care that we dropped it, and keeps allocating IDs past it regardless
    // of protocol, so this leaves a permanent gap. The next completely
    // unrelated server-created object - on any protocol, not just this one
    // - that needs the map to grow past that gap crashes the whole process
    // with a bare "Error reading events from display: Invalid argument".
    // This was the root cause of the "reload, then open a config dialog
    // twice" crash. See also winlist.c in wf-panel-pi, which shares this
    // fix.
    if (!global_registry)
    {
        GdkDisplay *gdk_display = gdk_display_get_default ();
        struct wl_display *display = gdk_wayland_display_get_wl_display (gdk_display);
        global_registry = wl_display_get_registry (display);
        wl_registry_add_listener (global_registry, &registry_listener, NULL);
    }

    g_signal_connect (wl->plugin, "style-updated", G_CALLBACK (theme_changed), wl);

    // Rebuild this instance's buttons (launchers plus one per already-known
    // window) from the persisted global window/launcher data - the
    // title/app_id/done sequence for a pre-existing window won't be resent
    // by the compositor, since the manager that originally received it is
    // never destroyed any more.
    wlist_update_display (wl);
}

void wlist_destructor (gpointer user_data)
{
    WinlistPlugin *wl = (WinlistPlugin *) user_data;

    g_signal_handlers_disconnect_by_data (wl->plugin, wl);

    if (active_plugin == wl) active_plugin = NULL;

    /* Tear down this instance's buttons only. The toplevel manager, its
     * handles, and the tracked window data in global_windows all persist
     * across reloads - see the comment in wlist_init() for why destroying
     * and recreating them here used to be able to crash the process. */
    if (wl->buttons) g_list_free_full (wl->buttons, (GDestroyNotify) destroy_button);
    wl->buttons = NULL;
    if (wl->box) gtk_widget_destroy (wl->box);
    wl->box = NULL;
    if (wl->drag) g_object_unref (wl->drag);
    wl->drag = NULL;

    g_free (wl);
}

/* End of file */
/*----------------------------------------------------------------------------*/
