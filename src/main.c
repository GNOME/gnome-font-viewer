/*
 * main.c
 *
 * Copyright (C) 2002-2003  James Henstridge <james@daa.com.au>
 * Copyright (C) 2010 Cosimo Cecchi <cosimoc@gnome.org>
 * Copyright 2022 Christopher Davis <christopherdavis@gnome.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <gio/gio.h>
#include <glib/gi18n.h>
#include "font-view.h"

static gboolean
_print_version_and_exit (const gchar *option_name,
                         const gchar *value,
                         gpointer data,
                         GError **error)
{
    g_print ("%s %s\n", _ ("GNOME Fonts"), VERSION);
    exit (EXIT_SUCCESS);
    return TRUE;
}

static const GOptionEntry goption_options[] = {
    {"version", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
     _print_version_and_exit, N_ ("Show the application's version"), NULL},
    {NULL}};

int
main (int argc, char **argv)
{
    g_autoptr (GApplication) app = NULL;
    gint retval;

    bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    app = font_view_application_new ();
    g_application_add_main_option_entries (app, goption_options);
    retval = g_application_run (app, argc, argv);

    return retval;
}
