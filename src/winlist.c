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

#define MAX_MENU_LEN 25

/*----------------------------------------------------------------------------*/
/* Global data                                                                */
/*----------------------------------------------------------------------------*/

conf_table_t conf_table[4] = {
    {CONF_TYPE_INT,     "max_width",    N_("Maximum width of task button"), NULL},
    {CONF_TYPE_BOOL,    "icons_only",   N_("Show only icons"),              NULL},
    {CONF_TYPE_INT,     "spacing",      N_("Item spacing"),                 NULL},
    {CONF_TYPE_NONE,    NULL,           NULL,                               NULL}
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
static void activate_handle (GtkWidget *, gpointer userdata);
static WindowBtn *find_btn (WinlistPlugin *wl, WindowItem *item);
static void create_button (WinlistPlugin *wl, WindowBtn *item);
static void destroy_button (WindowBtn *item);
static gboolean update_button_state (WindowBtn *item);
static void free_list_item (gpointer data);
static float score_match (const char *str1, const char *str2);
static char *get_exe (const char *cmdline);
static char *menu_cache_id (WinlistPlugin *wl, const char *app_id);
static void set_icon_and_title (WinlistPlugin *wl, WindowBtn *item);
static void update_item_width (WinlistPlugin *wl, WindowBtn *item);
static void popup_menu (GtkWidget *widget, gpointer userdata);
static void set_tooltip (WinlistPlugin *wl, WindowBtn *btn);
static void update_widths (WinlistPlugin *wl, int width);
static void update_icons (WinlistPlugin *wl);
static void update_size (GtkWidget *, GtkAllocation *alloc, gpointer userdata);
static gboolean idle_resize (gpointer userdata);
static gboolean handle_button_pressed (GtkWidget *widget, GdkEventButton *event, gpointer userdata);
static gboolean handle_button_release (GtkWidget *widget, GdkEventButton *event, gpointer userdata);
static void handle_gesture_end (GtkGestureLongPress *, GdkEventSequence *, gpointer userdata);
static void handle_drag_begin (GtkGestureDrag *, gdouble, gdouble, gpointer userdata);
static void handle_drag_update (GtkGestureDrag *, gdouble, gdouble, gpointer userdata);
static void handle_drag_end (GtkGestureDrag *, gdouble, gdouble, gpointer userdata);

/*----------------------------------------------------------------------------*/
/* Wayland protocol interface                                                 */
/*----------------------------------------------------------------------------*/

static void handle_toplevel_title (void *data, HANDLE_PTR handle, const char *title)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;
    WindowItem *item;
    GList *list;

    list = wl->windows;
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

static void handle_toplevel_app_id (void *data, HANDLE_PTR handle, const char *app_id)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;
    WindowItem *item;
    GList *list;

    list = wl->windows;
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

static void handle_toplevel_parent (void *data, HANDLE_PTR handle, HANDLE_PTR parent)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;
    WindowItem *item;
    GList *list;

    list = wl->windows;
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

static void handle_toplevel_state (void *data, HANDLE_PTR handle, struct wl_array *state)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;
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
}

static void handle_toplevel_done (void *data, HANDLE_PTR handle)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;
    WindowBtn *btn;
    GList *list;

    list = wl->windows;
    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (item->handle == (void *) handle)
        {
            if (item->title && item->app_id && !item->parent)
            {
                if (item->plugin)
                {
                    // button already exists - update the title
                    btn = find_btn (wl, item);
                    if (btn)
                    {
                        update_item_width (wl, btn);
                        update_button_state (btn);
                        gtk_widget_destroy (btn->icon);
                        set_icon_and_title (wl, btn);
                        set_tooltip (wl, btn);
                    }
                }
                else
                {
                    // new toplevel - look to see if its id is already associated with a button...
                    item->plugin = wl;
                    btn = find_btn (wl, item);
                    if (btn)
                    {
                        // found a button already for this app_id - update with new title, state etc
                        btn->windows++;
                        update_item_width (wl, btn);
                    }
                    else
                    {
                        // ...and if not, create one
                        btn = g_new0 (WindowBtn, 1);
                        btn->app_id = g_strdup (item->app_id);
                        btn->windows = 1;
                        btn->plugin = wl;
                        create_button (wl, btn);
                        gtk_widget_set_name (btn->btn, item->app_id);
                        wl->buttons = g_list_prepend (wl->buttons, btn);
                    }
                    update_button_state (btn);
                    set_tooltip (wl, btn);
                }
            }
            if (item->state & STATE_ACTIVATED && list != wl->windows)
            {
                // move this item to the front of the list if it is activated
                wl->windows = g_list_remove_link (wl->windows, list);
                wl->windows = g_list_concat (list, wl->windows);
            }
            break;
        }
        list = g_list_next (list);
    }
}

static void handle_toplevel_closed (void *data, HANDLE_PTR handle)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;
    WindowItem *item, *item2;
    WindowBtn *btn;
    GList *list, *btns;

    list = wl->windows;
    while (list)
    {
        item = (WindowItem *) list->data;
        if (item->handle == (void *) handle)
        {
            if (!item->parent)
            {
                btn = find_btn (wl, item);
                if (btn)
                {
                    btn->windows--;
                    if (!btn->windows)
                    {
                        destroy_button (btn);
                        btns = g_list_find (wl->buttons, btn);
                        wl->buttons = g_list_delete_link (wl->buttons, btns);
                    }
                    else
                    {
                        btns = wl->windows;
                        while (btns)
                        {
                            item2 = (WindowItem *) btns->data;
                            if (!g_strcmp0 (item2->app_id, btn->app_id))
                            {
                                if (btn->windows == 1) gtk_label_set_text (GTK_LABEL (btn->label), item2->title);
                                gtk_widget_destroy (btn->icon);
                                set_icon_and_title (wl, btn);
                                break;
                            }
                            btns = g_list_next (btns);
                        }
                    }
                }
            }
            if (item->title) g_free (item->title);
            if (item->app_id) g_free (item->app_id);
            wl->windows = g_list_delete_link (wl->windows, list);
            break;
        }
        list = g_list_next (list);
    }

    // force resize so buttons grow now there is more free space
    btns = wl->buttons;
    while (btns)
    {
        btn = (WindowBtn *) btns->data;
        update_button_state (btn);
        set_tooltip (wl, btn);
        if (!wl->icons_only && btn->btn) gtk_widget_set_size_request (btn->btn, wl->max_width, -1);
        btns = g_list_next (btns);
    }

    g_idle_add (idle_resize, wl);
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

    item->plugin = NULL;
    item->handle = (void *) toplevel;
    wl->windows = g_list_prepend (wl->windows, item);
    zwlr_foreign_toplevel_handle_v1_add_listener (toplevel, &toplevel_handle_v1, data);
}

static void handle_manager_finished (void *data, MANAGER_PTR)
{
    WinlistPlugin *wl = (WinlistPlugin*) data;
    wl->manager = NULL;
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

static void activate_app (GtkWidget *wid, gpointer userdata)
{
    GdkDisplay *gdk_display = gdk_display_get_default ();
    GdkSeat *seat = gdk_display_get_default_seat (gdk_display);
    struct wl_seat *wseat  = gdk_wayland_seat_get_wl_seat (seat);

    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    GList *list = g_list_last (wl->windows);
    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (!g_strcmp0 (gtk_widget_get_name (wid), item->app_id))
            zwlr_foreign_toplevel_handle_v1_activate (item->handle, wseat);
        list = list->prev;
    }
}

static void close_app (GtkWidget *wid, gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    GList *list = wl->windows;

    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (!g_strcmp0 (gtk_widget_get_name (wid), item->app_id))
            zwlr_foreign_toplevel_handle_v1_close (item->handle);
        list = list->next;
    }
}

static void maximise_app (GtkWidget *wid, gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    GList *list = wl->windows;

    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (!g_strcmp0 (gtk_widget_get_name (wid), item->app_id))
        {
            zwlr_foreign_toplevel_handle_v1_unset_minimized (item->handle);
            zwlr_foreign_toplevel_handle_v1_set_maximized (item->handle);
        }
        list = list->next;
    }
}

static void unmaximise_app (GtkWidget *wid, gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    GList *list = wl->windows;

    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (!g_strcmp0 (gtk_widget_get_name (wid), item->app_id))
        {
            zwlr_foreign_toplevel_handle_v1_unset_minimized (item->handle);
            zwlr_foreign_toplevel_handle_v1_unset_maximized (item->handle);
        }
        list = list->next;
    }
}

static void minimise_app (GtkWidget *wid, gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    GList *list = wl->windows;

    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (!g_strcmp0 (gtk_widget_get_name (wid), item->app_id))
            zwlr_foreign_toplevel_handle_v1_set_minimized (item->handle);
        list = list->next;
    }
}

static void unminimise_app (GtkWidget *wid, gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    GList *list = wl->windows;

    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (!g_strcmp0 (gtk_widget_get_name (wid), item->app_id))
            zwlr_foreign_toplevel_handle_v1_unset_minimized (item->handle);
        list = list->next;
    }
}

static void activate_handle (GtkWidget *, gpointer userdata)
{
    GdkDisplay *gdk_display = gdk_display_get_default ();
    GdkSeat *seat = gdk_display_get_default_seat (gdk_display);
    struct wl_seat *wseat  = gdk_wayland_seat_get_wl_seat (seat);

    zwlr_foreign_toplevel_handle_v1_activate ((HANDLE_PTR) userdata, wseat);
}

/*----------------------------------------------------------------------------*/
/* Button management                                                          */
/*----------------------------------------------------------------------------*/

static WindowBtn *find_btn (WinlistPlugin *wl, WindowItem *item)
{
    GList *btns = wl->buttons;
    while (btns)
    {
        WindowBtn *btn = (WindowBtn *) btns->data;
        if (!g_strcmp0 (btn->app_id, item->app_id)) return btn;
        btns = g_list_next (btns);
    }
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
    set_icon_and_title (wl, item);
    gtk_widget_show_all (wl->plugin);

    g_idle_add (idle_resize, wl);
}

static void destroy_button (WindowBtn *item)
{
    if (item->app_id) g_free (item->app_id);
    if (item->icon) gtk_widget_destroy (item->icon);
    if (item->label) gtk_widget_destroy (item->label);
    if (item->btn) gtk_widget_destroy (item->btn);
    if (item->gesture) g_object_unref (item->gesture);
    if (item->dgesture) g_object_unref (item->dgesture);
    item->app_id = NULL;
    item->icon = NULL;
    item->label = NULL;
    item->btn = NULL;
    item->gesture = NULL;
    item->dgesture = NULL;
}

static gboolean update_button_state (WindowBtn *btn)
{
    gboolean active = FALSE;
    GList *list = btn->plugin->windows;
    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (!g_strcmp0 (gtk_widget_get_name (btn->btn), item->app_id) && item->state & STATE_ACTIVATED) active = TRUE;
        list = list->next;
    }

    g_signal_handlers_block_by_func (btn->btn, G_CALLBACK (handle_button_pressed), btn->plugin);
    g_signal_handlers_block_by_func (btn->btn, G_CALLBACK (handle_button_release), btn->plugin);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn->btn), active);
    g_signal_handlers_unblock_by_func (btn->btn, G_CALLBACK (handle_button_pressed), btn->plugin);
    g_signal_handlers_unblock_by_func (btn->btn, G_CALLBACK (handle_button_release), btn->plugin);
    return FALSE;
}

static void free_list_item (gpointer data)
{
    WindowItem *item = (WindowItem *) data;
    if (item->title) g_free (item->title);
    if (item->app_id) g_free (item->app_id);
}

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
    MenuCacheItem *item;
    GSList *list, *iter;
    GAppInfo *info;
    char *id, *exec, *best = NULL;
    float res, score;
    const char *ex;

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
            if (best) g_free (best);
            g_slist_free_full (list, (GDestroyNotify) ((void *) menu_cache_item_unref));
            return id;
        }

        // didn't match - get the executable name
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

static void set_icon_and_title (WinlistPlugin *wl, WindowBtn *item)
{
    GtkWidget *box;
    char *str, *id;
    GAppInfo *info;
    GIcon *ic;
    MenuCacheItem *mitem;

    // create the desktop file name from the app_id
    str = g_strdup_printf ("%s.desktop", item->app_id);
    info = (GAppInfo *) g_desktop_app_info_new (str);
    g_free (str);

    if (info)
    {
        // the desktop file name is valid, so just get the icon from it
        ic = g_app_info_get_icon (info);
        str = g_icon_to_string (ic);
        g_object_unref (info);
    }
    else
    {
        // the desktop file name isn't valid, so search the menu cache for something similar
        id = menu_cache_id (wl, item->app_id);
        str = g_strdup_printf ("%s.desktop", id);
        g_free (id);
        mitem = menu_cache_find_item_by_id (wl->menu_cache, str);
        g_free (str);

        if (mitem)
        {
            str = g_strdup (menu_cache_item_get_icon (mitem));
            menu_cache_item_unref (mitem);
        }
        else str = NULL;
    }

    if (wl->icons_only)
    {
        item->icon = gtk_image_new ();
        gtk_container_add (GTK_CONTAINER (item->btn), item->icon);
//        wrap_set_taskbar_icon (wl, item->icon, str);

        GdkPixbuf *pb = load_taskbar_pixbuf (item->icon, str);
        cairo_surface_t *surf = gdk_cairo_surface_create_from_pixbuf (pb, 0, gtk_widget_get_window (wl->box));
        cairo_t *cr = cairo_create (surf);

        int dim = gdk_pixbuf_get_width (pb) / gtk_widget_get_scale_factor (item->btn);
		int fsize;
		if (dim == 48) fsize = 10;
		if (dim == 32) fsize = 7;
		if (dim == 24) fsize = 5;
		if (dim == 16) fsize = 3;

        cairo_set_source_rgb (cr, 1,1,1);
        cairo_arc (cr, dim - (dim / 6), dim - (dim / 6), dim / 6, 0, 6.3);
        cairo_fill (cr);
        cairo_set_source_rgb (cr, 0, 0, 0);

        //cairo_arc (cr, dim - (dim / 6), dim - (dim / 6), dim / 6, 0, 6.3);
        //cairo_set_line_width (cr, 0.5);
        //cairo_stroke (cr);

        //if (item->windows > 1)
        {
            cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size (cr, fsize);
            cairo_move_to (cr, dim - (dim / 4) + 1, dim - (dim / 12));
            char *buf = g_strdup_printf ("%d", item->windows);
            cairo_show_text (cr, buf);
            g_free (buf);
        }
        gtk_image_set_from_surface (GTK_IMAGE (item->icon), surf);
        cairo_surface_destroy (surf);

        gtk_widget_set_size_request (item->btn, -1, -1);
    }
    else
    {
        box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_container_add (GTK_CONTAINER (item->btn), box);

        item->icon = gtk_image_new ();
        gtk_container_add (GTK_CONTAINER (box), item->icon);
        wrap_set_taskbar_icon (wl, item->icon, str);

        item->label = gtk_label_new ("");
        gtk_label_set_xalign (GTK_LABEL (item->label), 0.0);
        gtk_container_add (GTK_CONTAINER (box), item->label);

        gtk_widget_show_all (box);

        update_item_width (wl, item);
    }
    gtk_widget_show_all (item->btn);

    g_free (str);
}

static void update_item_width (WinlistPlugin *wl, WindowBtn *btn)
{
    char *str, *title;
    int pref, min, tlen;
    GList *list;

    if (wl->icons_only)
    {
        gtk_widget_set_size_request (btn->btn, -1, -1);
        return;
    }

    gtk_widget_set_size_request (btn->btn, wl->item_width, -1);

    if (btn->windows > 1) title = _("<Multiple windows>");
    else
    {
        list = wl->windows;
        while (list)
        {
            WindowItem *item = (WindowItem *) list->data;
            if (!item->parent && !g_strcmp0 (item->app_id, btn->app_id))
            {
                 title = item->title;
                 break;
            }
            list = g_list_next (list);
        }
    }

    if (title)
    {
        str = g_strdup (title);
        for (tlen = strlen (str); tlen >= 0; tlen--)
        {
            if (tlen < (int) strlen (title))
            {
                if (tlen > 2) str[tlen - 3] = '.';
                if (tlen > 1) str[tlen - 2] = '.';
                if (tlen > 0) str[tlen - 1] = '.';
            }
            str[tlen] = 0;
            gtk_label_set_text (GTK_LABEL (btn->label), str);
            gtk_widget_get_preferred_width (btn->btn, &min, &pref);
            if (pref <= wl->item_width) break;
        }
        g_free (str);
    }
}

static void set_tooltip (WinlistPlugin *wl, WindowBtn *btn)
{
    const char *open, *close;
    char *tip = NULL, *tmp, *esc;
    GList *list = wl->windows;

    while (list)
    {
        WindowItem *item = (WindowItem *) list->data;
        if (!g_strcmp0 (item->app_id, btn->app_id))
        {
            if (item->state & STATE_MINIMISED && item->state & STATE_MAXIMISED)
            {
                open = "<b><i>";
                close = "</i></b>";
            }
            else if (item->state & STATE_MINIMISED)
            {
                open = "<i>";
                close = "</i>";
            }
            else if (item->state & STATE_MAXIMISED)
            {
                open = "<b>";
                close = "</b>";
            }
            else
            {
                open = "";
                close = "";
            }
            esc = g_markup_escape_text (item->title, -1);
            tmp = g_strdup_printf ("%s%s%s%s%s", tip ? tip : "", tip ? "\n" : "", open, esc, close);
            g_free (esc);
            if (tip) g_free (tip);
            tip = tmp;
        }
        list = list->next;
    }

    gtk_widget_set_tooltip_markup (btn->btn, tip);
    g_free (tip);
}

static void popup_menu (GtkWidget *widget, gpointer userdata)
{
    GtkWidget *menu, *item, *label;
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    const char *id = gtk_widget_get_name (widget);
    int count = 0;
    WindowItem *app;
    GList *list = wl->windows;
    char *str, *esc;
    gboolean min = FALSE, max = FALSE, unmin = FALSE, unmax = FALSE;

    while (list)
    {
        app = (WindowItem *) list->data;
        if (!g_strcmp0 (app->app_id, id))
        {
            if (app->state & STATE_MAXIMISED) unmax = TRUE;
            else max = TRUE;
            if (app->state & STATE_MINIMISED) unmin = TRUE;
            else min = TRUE;
            count++;
        }
        list = list->next;
    }

    menu = gtk_menu_new ();

    list = wl->windows;
    while (list)
    {
        app = (WindowItem *) list->data;
        if (!g_strcmp0 (app->app_id, id))
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
                label = gtk_bin_get_child (GTK_BIN (item));
                str = g_strdup_printf ("<i>%s</i>", gtk_label_get_text (GTK_LABEL (label)));
                gtk_label_set_markup (GTK_LABEL (label), str);
                g_free (str);
            }
            if (app->state & STATE_MAXIMISED)
            {
                label = gtk_bin_get_child (GTK_BIN (item));
                str = g_strdup_printf ("<b>%s</b>", gtk_label_get_label (GTK_LABEL (label)));
                gtk_label_set_markup (GTK_LABEL (label), str);
                g_free (str);
            }
            g_signal_connect (item, "activate", G_CALLBACK (activate_handle), (void *) app->handle);
            gtk_widget_set_tooltip_text (item, app->title);
            gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
        }
        list = list->next;
    }

    item = gtk_separator_menu_item_new ();
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

    if (min)
    {
        item = gtk_menu_item_new_with_label (count > 1 ? _("Hide All") : _("Hide"));
        gtk_widget_set_name (item, id);
        g_signal_connect (item, "activate", G_CALLBACK (minimise_app), userdata);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    }

    if (unmin)
    {
        item = gtk_menu_item_new_with_label (count > 1 ? _("Show All") : _("Show"));
        gtk_widget_set_name (item, id);
        g_signal_connect (item, "activate", G_CALLBACK (unminimise_app), userdata);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    }

    if (count == 1)
    {
        if (max)
        {
            item = gtk_menu_item_new_with_label (_("Maximise"));
            gtk_widget_set_name (item, id);
            g_signal_connect (item, "activate", G_CALLBACK (maximise_app), userdata);
            gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
        }

        if (unmax)
        {
            item = gtk_menu_item_new_with_label (_("Unmaximise"));
            gtk_widget_set_name (item, id);
            g_signal_connect (item, "activate", G_CALLBACK (unmaximise_app), userdata);
            gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
        }
    }

    item = gtk_separator_menu_item_new ();
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

    item = gtk_menu_item_new_with_label (count > 1 ? _("Close All") : _("Close"));
    gtk_widget_set_name (item, id);
    g_signal_connect (item, "activate", G_CALLBACK (close_app), userdata);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

    gtk_widget_show_all (menu);
    wrap_show_menu (widget, menu);
}

/*----------------------------------------------------------------------------*/
/* Layout control                                                             */
/*----------------------------------------------------------------------------*/

static void update_widths (WinlistPlugin *wl, int width)
{
    WindowBtn *item;
    GList *list;
    int target, count, oldwidth;

    count = g_list_length (wl->buttons);

    oldwidth = wl->item_width;
    target = width;
    target -= wl->spacing * (count - 1);
    target /= count;
    if (target >= wl->max_width) wl->item_width = wl->max_width;
    else wl->item_width = target;

    list = wl->buttons;
    while (list)
    {
        item = (WindowBtn *) list->data;
        update_item_width (wl, item);
        list = g_list_next (list);
    }

    if (oldwidth != wl->item_width) g_idle_add (idle_resize, wl);
}

static void update_icons (WinlistPlugin *wl)
{
    WindowBtn *item;
    GList *list, *children;

    list = wl->buttons;
    while (list)
    {
        item = (WindowBtn *) list->data;
        wl->item_width = wl->max_width;
        if (wl->icons_only) gtk_widget_set_size_request (item->btn, -1, -1);
        else gtk_widget_set_size_request (item->btn, wl->item_width, -1);
        children = gtk_container_get_children (GTK_CONTAINER (item->btn));
        g_list_free_full (children, (GDestroyNotify) gtk_widget_destroy);
        set_icon_and_title (wl, item);
        list = g_list_next (list);
    }

    gtk_box_set_spacing (GTK_BOX (wl->box), wl->spacing);
    gtk_widget_queue_allocate (wl->plugin);
}

static void update_size (GtkWidget *, GtkAllocation *alloc, gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    if (wl->dragon) return;
    if (alloc->width > 1) update_widths (wl, alloc->width);
}

static gboolean idle_resize (gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    gtk_widget_queue_resize (wl->plugin);
    return FALSE;
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

    if (wl->dragon)
    {
        GList *list = wl->buttons;
        while (list)
        {
            WindowBtn *btn = (WindowBtn *) list->data;
            if (btn->btn == wid)
            {
                g_idle_add ((GSourceFunc) update_button_state, btn);
                break;
            }
            list = list->next;
        }
        return FALSE;
    }

    if (pressed == PRESS_LONG) return FALSE;

    switch (event->button)
    {
        case 1:     activate_app (wid, userdata);
                    return FALSE;

        case 3:     popup_menu (wid, userdata);
                    return TRUE;
    }

    return FALSE;
}

static void handle_gesture_end (GtkGestureLongPress *, GdkEventSequence *, gpointer userdata)
{
    WindowBtn *btn = (WindowBtn *) userdata;

    if (btn->plugin->dragon) return;

    if (pressed == PRESS_LONG)
    {
        popup_menu (btn->btn, btn->plugin);
    }
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

    width = wl->icons_only ? get_icon_size (wl->plugin) : wl->item_width;

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
        index = index->next;
    }
    g_list_free (children);

    if (moveby >= 0) gtk_box_reorder_child (GTK_BOX (wl->box), wl->dragbtn, moveby);

    gtk_widget_queue_allocate (wl->plugin);
}

static void handle_drag_end (GtkGestureDrag *, gdouble, gdouble, gpointer userdata)
{
    WinlistPlugin *wl = (WinlistPlugin *) userdata;
    GtkStyleContext *sc;

    if (!wl->dragon) return;

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
    wl->drag = gdk_cursor_new_for_display (gdk_display_get_default (), GDK_HAND1);

    wl->dragon = FALSE;
    wl->windows = NULL;

    gboolean need_prefix = (g_getenv ("XDG_MENU_PREFIX") == NULL);
    wl->menu_cache = menu_cache_lookup (need_prefix ? "lxde-applications.menu+hidden" : "applications.menu+hidden");
    wl->reload_notify = menu_cache_add_reload_notify (wl->menu_cache, NULL, NULL);

    GdkDisplay *gdk_display = gdk_display_get_default ();
    struct wl_display *display = gdk_wayland_display_get_wl_display (gdk_display);
    struct wl_registry *registry = wl_display_get_registry (display);

    wl_registry_add_listener (registry, &registry_listener, wl);
    wl_display_roundtrip (display);
    wl_registry_destroy (registry);

    if (wl->manager) zwlr_foreign_toplevel_manager_v1_add_listener (wl->manager, &toplevel_manager_v1, wl);
}

static void close_handle (gpointer data, gpointer)
{
    WindowItem *item = (WindowItem *) data;
    zwlr_foreign_toplevel_handle_v1_destroy (item->handle);
}

void wlist_destructor (gpointer user_data)
{
    WinlistPlugin *wl = (WinlistPlugin *) user_data;

    // stop the window manager
    g_list_foreach (wl->windows, (GFunc) close_handle, wl);
    if (wl->manager) zwlr_foreign_toplevel_manager_v1_stop (wl->manager);

    if (wl->menu_cache)
    {
        menu_cache_remove_reload_notify (wl->menu_cache, wl->reload_notify);
        // unref'ing the menu cache causes a segfault because its io thread isn't being closed...
    }

    /* Deallocate memory */
    if (wl->windows) g_list_free_full (wl->windows, (GDestroyNotify) free_list_item);
    wl->windows = NULL;
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
