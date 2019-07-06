/* -*- mode: C; c-basic-offset: 4 -*-
 * gnome-font-viewer:
 *
 * Copyright (C) 2012 Cosimo Cecchi <cosimoc@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "font-utils.h"

#include "sushi-font-loader.h"

gchar *
font_utils_get_font_name (FT_Face face)
{
    if (g_strcmp0 (face->style_name, "Regular") == 0)
        return g_strdup (face->family_name);

    return g_strconcat (face->family_name, ", ", face->style_name, NULL);
}

gchar *
font_utils_get_font_name_for_file (FT_Library library,
                                   const gchar *path,
                                   gint face_index)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GFile) file = NULL;
    g_autofree gchar *uri = NULL, *contents = NULL;
    gchar *name = NULL;
    FT_Face face;

    file = g_file_new_for_path (path);
    uri = g_file_get_uri (file);

    face = sushi_new_ft_face_from_uri (library, uri, face_index, &contents,
                                       &error);
    if (error != NULL) {
        g_warning ("Can't get font name: %s", error->message);
        return NULL;
    }

    name = font_utils_get_font_name (face);
    FT_Done_Face (face);

    return name;
}
