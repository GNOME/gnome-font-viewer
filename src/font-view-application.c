/* -*- mode: C; c-basic-offset: 4 -*- */

/*
 * font-view: a font viewer for GNOME
 *
 * Copyright (C) 2002-2003  James Henstridge <james@daa.com.au>
 * Copyright (C) 2010 Cosimo Cecchi <cosimoc@gnome.org>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TYPE1_TABLES_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H
#include FT_MULTIPLE_MASTERS_H
#include <fontconfig/fontconfig.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <hb-ft.h>
#include <hb-ot.h>
#include <hb.h>
#include <libadwaita-1/adwaita.h>

#include "font-view-application.h"
#include "font-view-window.h"

struct _FontViewApplication
{
    AdwApplication parent;

    FontViewWindow *main_window;
};

G_DEFINE_TYPE (FontViewApplication,
               font_view_application,
               ADW_TYPE_APPLICATION);

static void
ensure_window (FontViewApplication *self)
{
    if (self->main_window == NULL)
        self->main_window = font_view_window_new (self);

    gtk_window_present (GTK_WINDOW (self->main_window));
}

static void
query_info_ready_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
    FontViewApplication *self = user_data;
    g_autoptr (GError) error = NULL;
    g_autoptr (GFileInfo) info = NULL;

    ensure_window (self);
    g_application_release (G_APPLICATION (self));

    info = g_file_query_info_finish (G_FILE (object), res, &error);
    if (error != NULL) {
        font_view_window_show_overview (self->main_window);
        font_view_window_show_error (self->main_window,
                                     _("Could Not Display Font"),
                                     error->message);
    } else {
        font_view_window_show_preview (self->main_window, G_FILE (object), 0);
    }
}

static void
font_view_application_open (GApplication *application,
                            GFile **files,
                            gint n_files,
                            const gchar *hint)
{
    FontViewApplication *self = FONT_VIEW_APPLICATION (application);

    g_application_hold (application);
    g_file_query_info_async (files[0], G_FILE_ATTRIBUTE_STANDARD_NAME,
                             G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT, NULL,
                             query_info_ready_cb, self);
}

static void
action_quit (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    FontViewApplication *self = user_data;
    gtk_window_close (GTK_WINDOW (self->main_window));
}

static void
action_about (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    FontViewApplication *self = user_data;

    // clang-format off
    const gchar *developers[] = {
        "Christopher Davis",
        "Evan Welsh",
        "Cosimo Cecchi",
        "James Henstridge",
        NULL
    };

    adw_show_about_dialog (GTK_WIDGET (self->main_window),
                           "version", VERSION,
                           "developers", developers,
                           "application-name", _("Fonts"),
                           "application-icon", FONT_VIEW_ICON_NAME,
                           "developer-name", _("The GNOME Project"),
                           "website", "https://gitlab.gnome.org/GNOME/gnome-font-viewer/",
                           "issue-url", "https://gitlab.gnome.org/GNOME/gnome-font-viewer/-/issues/",
                           "translator-credits", _("translator-credits"),
                           "license-type", GTK_LICENSE_GPL_2_0,
                           NULL);
    // clang-format on
}

static GActionEntry action_entries[] = {
    {"about", action_about, NULL, NULL, NULL},
    {"quit", action_quit, NULL, NULL, NULL}};

static void
font_view_application_startup (GApplication *application)
{
    FontViewApplication *self = FONT_VIEW_APPLICATION (application);

    G_APPLICATION_CLASS (font_view_application_parent_class)
        ->startup (application);

    if (!FcInit ())
        g_critical ("Can't initialize fontconfig library");

    g_action_map_add_action_entries (G_ACTION_MAP (self), action_entries,
                                     G_N_ELEMENTS (action_entries), self);
}

static void
font_view_application_activate (GApplication *application)
{
    FontViewApplication *self = FONT_VIEW_APPLICATION (application);

    ensure_window (self);
    font_view_window_show_overview (self->main_window);
}

static void
font_view_application_init (FontViewApplication *self)
{
    /* do nothing */
}

static void
font_view_application_class_init (FontViewApplicationClass *klass)
{
    GApplicationClass *aclass = G_APPLICATION_CLASS (klass);

    aclass->activate = font_view_application_activate;
    aclass->startup = font_view_application_startup;
    aclass->open = font_view_application_open;
}

GApplication *
font_view_application_new (void)
{
    return g_object_new (FONT_VIEW_TYPE_APPLICATION,
                         "application-id", APPLICATION_ID,
                         "flags", G_APPLICATION_HANDLES_OPEN,
                         "resource-base-path", "/org/gnome/font-viewer/",
                         NULL);
}
