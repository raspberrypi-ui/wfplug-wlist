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

#include "lxutils.h"

#include "winlist.h"

/*----------------------------------------------------------------------------*/
/* Typedefs and macros                                                        */
/*----------------------------------------------------------------------------*/

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

/*----------------------------------------------------------------------------*/
/* Function definitions                                                       */
/*----------------------------------------------------------------------------*/

static int get_state (WinlistPlugin *wl, struct zwlr_foreign_toplevel_handle_v1 *handle)
{
    GList *list = wl->windows;
    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (item->handle == (void *) handle)
        {
            return item->state;
        }
        list = g_list_next (list);
    }
    return 0;
}

static void activate_app (GtkWidget *, gpointer userdata)
{
    GdkDisplay *gdk_display = gdk_display_get_default ();
    GdkSeat *seat = gdk_display_get_default_seat (gdk_display);
    struct wl_seat *wseat  = gdk_wayland_seat_get_wl_seat (seat);

    zwlr_foreign_toplevel_handle_v1_activate ((struct zwlr_foreign_toplevel_handle_v1 *) userdata, wseat);
}

static void close_app (GtkWidget *, gpointer userdata)
{
    zwlr_foreign_toplevel_handle_v1_close ((struct zwlr_foreign_toplevel_handle_v1 *) userdata);
}

static void maximise_app (GtkWidget *, gpointer userdata)
{
    zwlr_foreign_toplevel_handle_v1_set_maximized ((struct zwlr_foreign_toplevel_handle_v1 *) userdata);
}

static void unmaximise_app (GtkWidget *, gpointer userdata)
{
    zwlr_foreign_toplevel_handle_v1_unset_maximized ((struct zwlr_foreign_toplevel_handle_v1 *) userdata);
}

static void minimise_app (GtkWidget *, gpointer userdata)
{
    zwlr_foreign_toplevel_handle_v1_set_minimized ((struct zwlr_foreign_toplevel_handle_v1 *) userdata);
}

static void unminimise_app (GtkWidget *, gpointer userdata)
{
    zwlr_foreign_toplevel_handle_v1_unset_minimized ((struct zwlr_foreign_toplevel_handle_v1 *) userdata);
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

static gboolean handle_button_release (GtkWidget *widget, GdkEventButton *event, gpointer userdata)
{
    switch (event->button)
    {
        case 1 :    return FALSE;

        case 3:     popup_menu (widget, userdata);
                    break;
    }

    return TRUE;
}
#if 0

    Glib::ustring shorten_title(int show_chars)
    {
        if (show_chars == 0)
        {
            return "";
        }

        int title_len = title.length();
        Glib::ustring short_title = title.substr(0, show_chars);
        if (title_len - show_chars >= 2)
        {
            short_title += "..";
        } else if (title_len != show_chars)
        {
            short_title += ".";
        }

        return short_title;
    }

    int get_button_preferred_width()
    {
        int min_width, preferred_width;
        button.get_preferred_width(min_width, preferred_width);

        return preferred_width;
    }

    void set_max_width(int width)
    {
        this->max_width = width;
        if (max_width == 0)
        {
            this->button.set_size_request(-1, -1);
            this->label.set_label(title);
            return;
        }

        this->button.set_size_request(width, -1);

        int show_chars = 0;
        for (show_chars = title.length(); show_chars > 0; show_chars--)
        {
            this->label.set_text(shorten_title(show_chars));
            if (get_button_preferred_width() <= max_width)
            {
                break;
            }
        }

        label.set_text(shorten_title(show_chars));
    }
#endif



static void set_icon_and_title (WinlistPlugin *wl, WindowItem *item)
{
    GtkWidget *icon, *label, *box;
    char *str;
    GAppInfo *info;
    GIcon *ic;

    str = g_strdup_printf ("%s.desktop", item->app_id);
    info = (GAppInfo *) g_desktop_app_info_new (str);
    ic = g_app_info_get_icon (info);
    g_free (str);

    str = g_icon_to_string (ic);
    icon = gtk_image_new ();
    wrap_set_taskbar_icon (wl, icon, str);
    g_free (str);

    if (wl->icons_only) gtk_container_add (GTK_CONTAINER (item->btn), icon);
    else
    {
        label = gtk_label_new (item->title);

        box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_container_add (GTK_CONTAINER (box), icon);
        gtk_container_add (GTK_CONTAINER (box), label);
        gtk_container_add (GTK_CONTAINER (item->btn), box);
    }
    gtk_widget_show_all (item->btn);
    if (item->title) gtk_widget_set_tooltip_text (item->btn, item->title);
}

static void handle_toplevel_title (void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, const char *title)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;
    GList *child, *list = wl->windows;
    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (item->handle == (void *) handle)
        {
            item->title = g_strdup (title);
            if (item->btn)
            {
                child = gtk_container_get_children (GTK_CONTAINER (item->btn));
                gtk_widget_destroy (child->data);
                set_icon_and_title (wl, item);
            }
            break;
        }
        list = g_list_next (list);
    }
}

static void handle_toplevel_app_id (void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, const char *app_id)
{
    char *str;
    WinlistPlugin *wl = (WinlistPlugin*) data;
    GList *list = wl->windows;
    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (item->handle == (void *) handle)
        {
            item->app_id = g_strdup (app_id);
            item->btn = gtk_toggle_button_new ();
            g_signal_connect (item->btn, "clicked", G_CALLBACK (activate_app), handle);
            g_signal_connect (item->btn, "button-release-event", G_CALLBACK (handle_button_release), item);
            set_icon_and_title (wl, item);
            gtk_container_add (GTK_CONTAINER (wl->plugin), item->btn);
            gtk_widget_show_all (wl->plugin);
            break;
        }
        list = g_list_next (list);
    }
}

static void handle_toplevel_output_enter (void *data, struct zwlr_foreign_toplevel_handle_v1 *, struct wl_output *output)
{
}

static void handle_toplevel_output_leave (void *data, struct zwlr_foreign_toplevel_handle_v1 *, struct wl_output *output)
{
}

static void handle_toplevel_state (void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, struct wl_array *state)
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
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (item->btn), item->state & STATE_ACTIVATED);
        list = g_list_next (list);
    }
}

static void handle_toplevel_done (void *data, struct zwlr_foreign_toplevel_handle_v1 *)
{
}

static void handle_toplevel_closed (void *data, struct zwlr_foreign_toplevel_handle_v1 *handle)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;
    GList *list = wl->windows;

    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (item->handle == (void *) handle)
        {
            if (item->btn) gtk_widget_destroy (item->btn);
            g_free (item->title);
            g_free (item->app_id);
            wl->windows = g_list_delete_link (wl->windows, list);
            break;
        }
        list = g_list_next (list);
    }
}

static void handle_toplevel_parent (void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, struct zwlr_foreign_toplevel_handle_v1 *parent)
{
}

struct zwlr_foreign_toplevel_handle_v1_listener toplevel_handle_v1_impl = {
    .title  = handle_toplevel_title,
    .app_id = handle_toplevel_app_id,
    .output_enter = handle_toplevel_output_enter,
    .output_leave = handle_toplevel_output_leave,
    .state  = handle_toplevel_state,
    .done   = handle_toplevel_done,
    .closed = handle_toplevel_closed,
    .parent = handle_toplevel_parent
};

static void handle_manager_toplevel (void *data, struct zwlr_foreign_toplevel_manager_v1 *manager,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;
    WindowItem *item = g_new0 (WindowItem, 1);

    item->handle = (void *) toplevel;
    wl->windows = g_list_append (wl->windows, item);
        
    zwlr_foreign_toplevel_handle_v1_add_listener(toplevel, &toplevel_handle_v1_impl, data);
}

static void handle_manager_finished (void *data, struct zwlr_foreign_toplevel_manager_v1 *manager)
{}

struct zwlr_foreign_toplevel_manager_v1_listener toplevel_manager_v1_impl = {
    .toplevel = handle_manager_toplevel,
    .finished = handle_manager_finished,
};

static void registry_add_object (void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;

    if (!g_strcmp0 (interface, zwlr_foreign_toplevel_manager_v1_interface.name))
    {
        wl->manager = (struct zwlr_foreign_toplevel_manager_v1*) wl_registry_bind (registry, name, &zwlr_foreign_toplevel_manager_v1_interface, version < 3 ? version : 3);
    }
}

static void registry_remove_object (void *data, struct wl_registry *registry, uint32_t name)
{
}

static struct wl_registry_listener registry_listener =
{
    &registry_add_object,
    &registry_remove_object
};


static void update_icons (WinlistPlugin *wl)
{
    GList *child, *list = wl->windows;

    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (item->btn)
        {
            child = gtk_container_get_children (GTK_CONTAINER (item->btn));
            gtk_widget_destroy (child->data);
            set_icon_and_title (wl, item);
        }
        list = g_list_next (list);
    }
}

/*----------------------------------------------------------------------------*/
/* wf-panel plugin functions                                                  */
/*----------------------------------------------------------------------------*/

/* Handler for button click */
static void wlist_button_clicked (GtkWidget *, WinlistPlugin *wl)
{
    CHECK_LONGPRESS
}

/* Handler for system config changed message from panel */
void wlist_update_display (WinlistPlugin *wl)
{
    update_icons (wl);
}

/* Handler for control message */
gboolean wlist_control_msg (WinlistPlugin *wl, const char *cmd)
{
    return FALSE;
}

void wlist_init (WinlistPlugin *wl)
{
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

    /* Set up variables */
    //gtk_box_set_spacing (GTK_BOX (lch->plugin), lch->spacing);

    wl->windows = NULL;

    GdkDisplay *gdk_display = gdk_display_get_default ();
    struct wl_display *display = gdk_wayland_display_get_wl_display (gdk_display);

    struct wl_registry *registry = wl_display_get_registry (display);
    wl_registry_add_listener (registry, &registry_listener, wl);
    wl_display_roundtrip (display);

    if (!wl->manager)
    {
        printf ("no manager\n");
        wl_registry_destroy (registry);
        return;
    }

    wl_registry_destroy (registry);
    zwlr_foreign_toplevel_manager_v1_add_listener (wl->manager, &toplevel_manager_v1_impl, wl);
}

void wlist_destructor (gpointer user_data)
{
    WinlistPlugin *wl = (WinlistPlugin *) user_data;

    /* Deallocate memory */
    g_free (wl);
}

/* End of file */
/*----------------------------------------------------------------------------*/
