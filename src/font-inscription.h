/*
 * Copyright © 2025 Khalid Abu Shawarib
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
 *
 * Authors: Khalid Abu Shawarib <kas@gnome.org>
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define FONT_TYPE_INSCRIPTION (font_inscription_get_type ())

G_DECLARE_FINAL_TYPE (FontInscription, font_inscription, FONT, INSCRIPTION, GtkWidget)

GtkWidget *             font_inscription_new                     (const char             *text);

const char *            font_inscription_get_text                (FontInscription         *self);
void                    font_inscription_set_text                (FontInscription         *self,
                                                                  const char             *text);
PangoAttrList *         font_inscription_get_attributes          (FontInscription         *self);
void                    font_inscription_set_attributes          (FontInscription         *self,
                                                                  PangoAttrList          *attrs);

G_END_DECLS
