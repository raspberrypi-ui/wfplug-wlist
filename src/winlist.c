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

#include "winlist.h"

/*----------------------------------------------------------------------------*/
/* Typedefs and macros                                                        */
/*----------------------------------------------------------------------------*/

#define HANDLE_PTR struct zwlr_foreign_toplevel_handle_v1 *
#define MANAGER_PTR struct zwlr_foreign_toplevel_manager_v1 *

#define MAXWIDTH 800

/*----------------------------------------------------------------------------*/
/* Global data                                                                */
/*----------------------------------------------------------------------------*/

conf_table_t conf_table[4] = {
    {CONF_TYPE_INT,     "spacing",      N_("Icon spacing"),     NULL},
    {CONF_TYPE_INT,     "max_width",    N_("Max item width"),   NULL},
    {CONF_TYPE_BOOL,    "icons_only",   N_("Show only icons"),  NULL},
    {CONF_TYPE_NONE,    NULL,           NULL,                   NULL}
};

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static void activate_app (GtkWidget *, gpointer userdata);
static void close_app (GtkWidget *, gpointer userdata);
static void maximise_app (GtkWidget *, gpointer userdata);
static void unmaximise_app (GtkWidget *, gpointer userdata);
static void minimise_app (GtkWidget *, gpointer userdata);
static void unminimise_app (GtkWidget *, gpointer userdata);
static void update_item_width (WinlistPlugin *wl, WindowItem *item);
static void set_icon_and_title (WinlistPlugin *wl, WindowItem *item);
static void update_widths (WinlistPlugin *wl, int width);
static void create_button (WinlistPlugin *wl, WindowItem *item);
static void update_icons (WinlistPlugin *wl);
static gboolean handle_button_pressed (GtkWidget *widget, GdkEventButton *event, gpointer userdata);
static gboolean handle_button_release (GtkWidget *widget, GdkEventButton *event, gpointer userdata);
static void handle_gesture_end (GtkGestureLongPress *, GdkEventSequence *, gpointer userdata);
static void handle_drag_begin (GtkGestureDrag *, gdouble, gdouble, gpointer userdata);
static void handle_drag_update (GtkGestureDrag *, gdouble, gdouble, gpointer userdata);
static void handle_drag_end (GtkGestureDrag *, gdouble, gdouble, gpointer userdata);
static void popup_menu (GtkWidget *widget, gpointer userdata);

/*----------------------------------------------------------------------------*/
/* Wayland protocol interface                                                 */
/*----------------------------------------------------------------------------*/

static void handle_toplevel_title (void *data, HANDLE_PTR handle, const char *title)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;
    GList *list = wl->windows;

    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (item->handle == (void *) handle)
        {
            if (!item->title)
            {
                item->title = g_strdup (title);
                if (item->app_id) create_button (wl, item);
            }
            else
            {
                g_free (item->title);
                item->title = g_strdup (title);
                update_item_width (wl, item);
            }
            break;
        }
        list = g_list_next (list);
    }
}

static void handle_toplevel_app_id (void *data, HANDLE_PTR handle, const char *app_id)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;
    GList *list = wl->windows;

    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (item->handle == (void *) handle)
        {
            item->app_id = g_strdup (app_id);
            if (item->title) create_button (wl, item);
            break;
        }
        list = g_list_next (list);
    }
}

static void handle_toplevel_parent (void *data, HANDLE_PTR handle, HANDLE_PTR parent)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;
    GList *list = wl->windows;

    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (item->handle == (void *) handle)
        {
            item->parent = (void *) parent;
            if (item->parent && item->btn)
            {
                if (item->icon) gtk_widget_destroy (item->icon);
                if (item->label) gtk_widget_destroy (item->label);
                if (item->btn) gtk_widget_destroy (item->btn);
                if (item->gesture) g_object_unref (item->gesture);
                item->icon = NULL;
                item->label = NULL;
                item->btn = NULL;
                item->gesture = NULL;
            }
            break;
        }
        list = g_list_next (list);
    }
}

static void handle_toplevel_state (void *data, HANDLE_PTR handle, struct wl_array *state)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;
    int flags = 0;
    uint32_t *arr;
    WindowItem *item;
    GList *list;

    wl_array_for_each (arr, state)
    {
        if (*arr == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED)
            flags |= STATE_ACTIVATED;

        if (*arr == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED)
            flags |= STATE_MAXIMISED;

        if (*arr == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED)
            flags |= STATE_MINIMISED;
    }

    list = wl->windows;
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

    list = wl->windows;
    while (list)
    {
        item = (WindowItem *) list->data;
        if (item->btn)
        {
            g_signal_handlers_block_by_func (item->btn, G_CALLBACK (handle_button_pressed), item);
            g_signal_handlers_block_by_func (item->btn, G_CALLBACK (handle_button_release), item);
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (item->btn), item->state & STATE_ACTIVATED);
            g_signal_handlers_unblock_by_func (item->btn, G_CALLBACK (handle_button_pressed), item);
            g_signal_handlers_unblock_by_func (item->btn, G_CALLBACK (handle_button_release), item);
        }
        list = g_list_next (list);
    }
}

static void handle_toplevel_closed (void *data, HANDLE_PTR handle)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;
    GList *list = wl->windows;

    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (item->handle == (void *) handle)
        {
            if (item->icon) gtk_widget_destroy (item->icon);
            if (item->label) gtk_widget_destroy (item->label);
            if (item->btn) gtk_widget_destroy (item->btn);
            if (item->gesture) g_object_unref (item->gesture);
            if (item->title) g_free (item->title);
            if (item->app_id) g_free (item->app_id);
            wl->windows = g_list_delete_link (wl->windows, list);
        }

        // force resize so buttons grow now there is more free space
        if (!wl->icons_only && item->btn) gtk_widget_set_size_request (item->btn, wl->max_width, -1);

        list = g_list_next (list);
    }
}

static void handle_toplevel_done (void *, HANDLE_PTR)
{
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

static void handle_manager_toplevel (void *data, MANAGER_PTR, HANDLE_PTR toplevel)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;
    WindowItem *item = g_new0 (WindowItem, 1);

    item->plugin = wl;
    item->handle = (void *) toplevel;
    wl->windows = g_list_append (wl->windows, item);
    zwlr_foreign_toplevel_handle_v1_add_listener (toplevel, &toplevel_handle_v1, data);
}

static void handle_manager_finished (void *, MANAGER_PTR)
{
}

struct zwlr_foreign_toplevel_manager_v1_listener toplevel_manager_v1 = 
{
    .toplevel = handle_manager_toplevel,
    .finished = handle_manager_finished,
};

static void registry_add_object (void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;

    if (!g_strcmp0 (interface, zwlr_foreign_toplevel_manager_v1_interface.name))
    {
        wl->manager = (MANAGER_PTR) wl_registry_bind (registry, name, &zwlr_foreign_toplevel_manager_v1_interface, version < 3 ? version : 3);
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

static void activate_app (GtkWidget *, gpointer userdata)
{
    GdkDisplay *gdk_display = gdk_display_get_default ();
    GdkSeat *seat = gdk_display_get_default_seat (gdk_display);
    struct wl_seat *wseat  = gdk_wayland_seat_get_wl_seat (seat);

    zwlr_foreign_toplevel_handle_v1_activate ((HANDLE_PTR) userdata, wseat);
}

static void close_app (GtkWidget *, gpointer userdata)
{
    zwlr_foreign_toplevel_handle_v1_close ((HANDLE_PTR) userdata);
}

static void maximise_app (GtkWidget *, gpointer userdata)
{
    zwlr_foreign_toplevel_handle_v1_set_maximized ((HANDLE_PTR) userdata);
}

static void unmaximise_app (GtkWidget *, gpointer userdata)
{
    zwlr_foreign_toplevel_handle_v1_unset_maximized ((HANDLE_PTR) userdata);
}

static void minimise_app (GtkWidget *, gpointer userdata)
{
    zwlr_foreign_toplevel_handle_v1_set_minimized ((HANDLE_PTR) userdata);
}

static void unminimise_app (GtkWidget *, gpointer userdata)
{
    zwlr_foreign_toplevel_handle_v1_unset_minimized ((HANDLE_PTR) userdata);
}

/*----------------------------------------------------------------------------*/
/* Button management                                                          */
/*----------------------------------------------------------------------------*/

static void update_item_width (WinlistPlugin *wl, WindowItem *item)
{
    char *str;
    size_t tlen;
    int pref, min;

    if (wl->icons_only)
    {
        gtk_widget_set_size_request (item->btn, wl->item_width, -1);
        return;
    }

    gtk_widget_set_size_request (item->btn, wl->item_width, -1);

    if (item->title)
    {
        str = g_strdup (item->title);
        for (tlen = strlen (str); tlen > 0; tlen--)
        {
            if (tlen < strlen (item->title))
            {
                if (tlen > 2) str[tlen - 3] = '.';
                if (tlen > 1) str[tlen - 2] = '.';
                if (tlen > 0) str[tlen - 1] = '.';
            }
            str[tlen] = 0;
            gtk_label_set_text (GTK_LABEL (item->label), str);
            gtk_widget_get_preferred_width (item->btn, &min, &pref);
            if (pref <= wl->item_width) break;
        }
        g_free (str);
    }
}

static void set_icon_and_title (WinlistPlugin *wl, WindowItem *item)
{
    GtkWidget *box;
    char *str;
    GAppInfo *info;
    GIcon *ic;
    GSList *list, *iter;
    MenuCacheItem *mitem;

    str = g_strdup_printf ("%s.desktop", item->app_id);
    info = (GAppInfo *) g_desktop_app_info_new (str);
    g_free (str);

    str = NULL;
    if (info)
    {
        ic = g_app_info_get_icon (info);
        str = g_icon_to_string (ic);
    }
    else
    {
        // the app-id doesn't directly match anything, so see if an ID in the menu cache list contains it
        list = menu_cache_list_all_apps (wl->menu_cache);
        iter = list;
        while (iter)
        {
            mitem = (MenuCacheItem *) iter->data;
            if (strcasestr (menu_cache_item_get_id (mitem), item->app_id))
            {
                str = g_strdup (menu_cache_item_get_icon (mitem));
                break;
            }
            iter = iter->next;
        }
        g_slist_free_full (list, (GDestroyNotify) ((void *) menu_cache_item_unref));
    }

    // if we didn't find any matches, just use a default icon
    if (!str) str = g_strdup ("application-x-executable");

    item->icon = gtk_image_new ();
    wrap_set_taskbar_icon (wl, item->icon, str);
    g_free (str);

    if (wl->icons_only)
    {
        gtk_container_add (GTK_CONTAINER (item->btn), item->icon);
        gtk_widget_set_size_request (item->btn, -1, -1);
    }
    else
    {
        item->label = gtk_label_new (item->title);

        box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_container_add (GTK_CONTAINER (box), item->icon);
        gtk_container_add (GTK_CONTAINER (box), item->label);
        gtk_container_add (GTK_CONTAINER (item->btn), box);

        gtk_widget_show_all (box);

        update_item_width (wl, item);
    }
    gtk_widget_show_all (item->btn);

    if (item->title) gtk_widget_set_tooltip_text (item->btn, item->title);
}

static void update_widths (WinlistPlugin *wl, int width)
{
    WindowItem *item;
    GList *list;
    int target, count = 0;

    list = wl->windows;
    while (list)
    {
        item = (WindowItem *) list->data;
        if (item->btn) count++;
        list = g_list_next (list);
    }

    target = width;
    target -= wl->spacing * (count - 1);
    target /= count;
    if (target >= wl->max_width) wl->item_width = wl->max_width;
    else wl->item_width = target;

    list = wl->windows;
    while (list)
    {
        item = (WindowItem *) list->data;
        if (item->btn) update_item_width (wl, item);
        list = g_list_next (list);
    }
}

static void create_button (WinlistPlugin *wl, WindowItem *item)
{
    GtkGesture *drag;

    if (!item->parent)
    {
        item->btn = gtk_toggle_button_new ();
        item->gesture = add_long_press (item->btn, G_CALLBACK (handle_gesture_end), item);
        g_signal_connect (item->btn, "button-press-event", G_CALLBACK (handle_button_pressed), wl);
        g_signal_connect (item->btn, "button-release-event", G_CALLBACK (handle_button_release), item);

        drag = gtk_gesture_drag_new (item->btn);
        g_signal_connect (drag, "drag-begin", G_CALLBACK (handle_drag_begin), wl);
        g_signal_connect (drag, "drag-update", G_CALLBACK (handle_drag_update), wl);
        g_signal_connect (drag, "drag-end", G_CALLBACK (handle_drag_end), wl);

        set_icon_and_title (wl, item);
        gtk_container_add (GTK_CONTAINER (wl->box), item->btn);
        gtk_widget_show_all (wl->plugin);
    }
}

static void update_icons (WinlistPlugin *wl)
{
    // might still need work...
    WindowItem *item;
    GList *list, *children;

    list = wl->windows;
    while (list)
    {
        item = (WindowItem *) list->data;
        if (item->btn)
        {
            children = gtk_container_get_children (GTK_CONTAINER (item->btn));
            g_list_free_full (children, (GDestroyNotify) gtk_widget_destroy);
            set_icon_and_title (wl, item);
        }
        list = g_list_next (list);
    }

    gtk_box_set_spacing (GTK_BOX (wl->box), wl->spacing);
}

/*----------------------------------------------------------------------------*/
/* Handlers                                                                   */
/*----------------------------------------------------------------------------*/

static gboolean handle_button_pressed (GtkWidget *self, GdkEventButton *, gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    wl->dragbtn = self;
    pressed = PRESS_NONE;
    return FALSE;
}

static gboolean handle_button_release (GtkWidget *widget, GdkEventButton *event, gpointer userdata)
{
    WindowItem *win = (WindowItem *) userdata;

    if (win->plugin->noclick)
    {
        win->plugin->noclick = FALSE;
        return FALSE;
    }

    if (pressed == PRESS_LONG) return FALSE;

    switch (event->button)
    {
        case 1:     activate_app (widget, win->handle);
                    return FALSE;

        case 3:     popup_menu (widget, userdata);
                    return TRUE;
    }

    return FALSE;
}

static void handle_gesture_end (GtkGestureLongPress *, GdkEventSequence *, gpointer userdata)
{
    WindowItem *item = (WindowItem *) userdata;

    if (pressed == PRESS_LONG)
    {
        popup_menu (item->btn, userdata);
    }
}

static void handle_drag_begin (GtkGestureDrag *, gdouble, gdouble, gpointer userdata)
{
}

static void handle_drag_update (GtkGestureDrag *, gdouble x, gdouble y, gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    GList *children, *index;
    int moveby;
    int width;
    GtkAllocation alloc;

    wl->noclick = TRUE;

    width = wl->icons_only ? get_icon_size (wl->plugin) : wl->item_width;

    children = gtk_container_get_children (GTK_CONTAINER (wl->box));
    index = children;
    moveby = x / width;
    while (index)
    {
        if (index->data == wl->dragbtn) break;
        moveby++;
        index = index->next;
    }

    gtk_box_reorder_child (GTK_BOX (wl->box), wl->dragbtn, moveby);
    gtk_widget_queue_draw (wl->box);
}

static void handle_drag_end (GtkGestureDrag *self, gdouble x, gdouble y, gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;

    wl->noclick = FALSE;

    // update the list here!!!!
}

static void popup_menu (GtkWidget *widget, gpointer userdata)
{
    GtkWidget *menu, *item;
    WindowItem *win = (WindowItem *) userdata;

    menu = gtk_menu_new ();

    if (win->state & STATE_MINIMISED)
    {
        item = gtk_menu_item_new_with_label (_("Unminimise"));
        g_signal_connect (item, "activate", G_CALLBACK (unminimise_app), win->handle);
    }
    else
    {
        item = gtk_menu_item_new_with_label (_("Minimise"));
        g_signal_connect (item, "activate", G_CALLBACK (minimise_app), win->handle);
    }
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

    if (win->state & STATE_MAXIMISED)
    {
        item = gtk_menu_item_new_with_label (_("Unmaximise"));
        g_signal_connect (item, "activate", G_CALLBACK (unmaximise_app), win->handle);
    }
    else
    {
        item = gtk_menu_item_new_with_label (_("Maximise"));
        g_signal_connect (item, "activate", G_CALLBACK (maximise_app), win->handle);
    }
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

    item = gtk_menu_item_new_with_label (_("Close"));
    g_signal_connect (item, "activate", G_CALLBACK (close_app), win->handle);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

    gtk_widget_show_all (menu);
    gtk_menu_popup_at_widget (GTK_MENU (menu), widget, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
}

static void update_size (GtkWidget *, GtkAllocation *alloc, gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    if (wl->noclick) return;
    if (alloc->width > 1) update_widths (wl, alloc->width);
}

/*----------------------------------------------------------------------------*/
/* wf-panel plugin functions                                                  */
/*----------------------------------------------------------------------------*/

/* Handler for system config changed message from panel */
void wlist_update_display (WinlistPlugin *wl)
{
    update_icons (wl);
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
    wl->item_width = wl->max_width;
    wl->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, wl->spacing);
    gtk_box_set_homogeneous (GTK_BOX (wl->box), TRUE);
    gtk_box_set_spacing (GTK_BOX (wl->box), wl->spacing);
    gtk_container_add (GTK_CONTAINER (wl->plugin), wl->box);
    g_signal_connect (wl->plugin, "size-allocate", G_CALLBACK (update_size), wl);

    wl->windows = NULL;

    gboolean need_prefix = (g_getenv ("XDG_MENU_PREFIX") == NULL);
    wl->menu_cache = menu_cache_lookup (need_prefix ? "lxde-applications.menu" : "applications.menu");
    menu_cache_add_reload_notify (wl->menu_cache, NULL, NULL);

    GdkDisplay *gdk_display = gdk_display_get_default ();
    struct wl_display *display = gdk_wayland_display_get_wl_display (gdk_display);
    struct wl_registry *registry = wl_display_get_registry (display);

    wl_registry_add_listener (registry, &registry_listener, wl);
    wl_display_roundtrip (display);
    wl_registry_destroy (registry);

    if (wl->manager) zwlr_foreign_toplevel_manager_v1_add_listener (wl->manager, &toplevel_manager_v1, wl);
}

void wlist_destructor (gpointer user_data)
{
    WinlistPlugin *wl = (WinlistPlugin *) user_data;

    /* Deallocate memory */
    g_free (wl);
}

/* End of file */
/*----------------------------------------------------------------------------*/
