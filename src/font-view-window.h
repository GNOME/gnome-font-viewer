/*
 * font-view-window.h
 *
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

#pragma once

#include <adwaita.h>

#include "font-view.h"

G_BEGIN_DECLS

#define FONT_VIEW_TYPE_WINDOW (font_view_window_get_type ())

G_DECLARE_FINAL_TYPE (FontViewWindow, font_view_window, FONT_VIEW, WINDOW, AdwApplicationWindow)

FontViewWindow * font_view_window_new (FontViewApplication *app);

void font_view_window_show_overview (FontViewWindow *self);
void font_view_window_show_preview  (FontViewWindow *self,
                                     GFile          *file,
                                     int             face_index);
void font_view_window_show_error    (FontViewWindow *self,
                                     const char     *heading,
                                     const char     *body);

G_END_DECLS
