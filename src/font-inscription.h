/*
 * Copyright © 2025 Khalid Abu Shawarib
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Authors: Khalid Abu Shawarib <kas@gnome.org>
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define FONT_TYPE_INSCRIPTION (font_inscription_get_type ())

G_DECLARE_FINAL_TYPE (FontInscription, font_inscription, FONT, INSCRIPTION, GtkWidget)

GtkWidget *             font_inscription_new                     (const char              *text,
                                                                  PangoAttrList           *attributes);

const char *            font_inscription_get_text                (FontInscription         *self);
void                    font_inscription_set_text                (FontInscription         *self,
                                                                  const char              *text);
PangoAttrList *         font_inscription_get_attributes          (FontInscription         *self);
void                    font_inscription_set_attributes          (FontInscription         *self,
                                                                  PangoAttrList           *attrs);

G_END_DECLS
