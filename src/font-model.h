/* -*- mode: C; c-basic-offset: 4 -*-
 * gnome-font-viewer
 *
 * Copyright (C) 2012 Cosimo Cecchi <cosimoc@gnome.org>
 *
 * based on font-method.c code from
 *
 * fontilus - a collection of font utilities for GNOME
 * Copyright (C) 2002-2003  James Henstridge <james@daa.com.au>
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

#ifndef __FONT_VIEW_MODEL_H__
#define __FONT_VIEW_MODEL_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef enum {
  COLUMN_NAME,
  COLUMN_PATH,
  COLUMN_FACE_INDEX,
  COLUMN_ICON,
  COLUMN_COLLATION_KEY,
  NUM_COLUMNS
} FontViewModelColumns;

#define FONT_VIEW_TYPE_MODEL (font_view_model_get_type ())
G_DECLARE_FINAL_TYPE (FontViewModel, font_view_model,
                      FONT_VIEW, MODEL,
                      GObject)

FontViewModel * font_view_model_new (void);

gboolean font_view_model_has_face (FontViewModel *self,
                                   FT_Face face);
GListModel *font_view_model_get_list_model (FontViewModel *self);

#define FONT_VIEW_TYPE_MODEL_ITEM (font_view_model_item_get_type ())
G_DECLARE_FINAL_TYPE (FontViewModelItem, font_view_model_item,
                      FONT_VIEW, MODEL_ITEM,
                      GObject)

gint font_view_model_item_get_face_index (FontViewModelItem *self);
const gchar *font_view_model_item_get_collation_key (FontViewModelItem *self);
const gchar *font_view_model_item_get_font_name (FontViewModelItem *self);
const gchar *font_view_model_item_get_font_path (FontViewModelItem *self);

G_END_DECLS

#endif /* __FONT_VIEW_MODEL_H__ */
