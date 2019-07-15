/* -*- mode: C; c-basic-offset: 4 -*-
 * gnome-font-view: 
 *
 * Copyright (C) 2012 Cosimo Cecchi <cosimoc@gnome.org>
 *
 * based on code from
 *
 * fontilus - a collection of font utilities for GNOME
 * Copyright (C) 2002-2003  James Henstridge <james@daa.com.au>
 * 
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

#include <gio/gio.h>
#include <gtk/gtk.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>

#include "font-model.h"
#include "font-utils.h"

struct _FontViewModel
{
    GObject parent_instance;

    /* list of fonts in fontconfig database */
    FcFontSet *font_list;
    GMutex font_list_mutex;

    FT_Library library;

    GListStore *model;
    GCancellable *cancellable;

    guint font_list_idle_id;
    guint fontconfig_update_id;
};

G_DEFINE_TYPE (FontViewModel, font_view_model, G_TYPE_OBJECT)

struct _FontViewModelItem
{
    GObject parent_instance;

    gchar *collation_key;
    gchar *font_name;
    GFile *file;
    int face_index;
};

G_DEFINE_TYPE (FontViewModelItem, font_view_model_item, G_TYPE_OBJECT)

static void
font_view_model_item_finalize (GObject *obj)
{
    FontViewModelItem *self = FONT_VIEW_MODEL_ITEM (obj);

    g_clear_pointer (&self->collation_key, g_free);
    g_clear_pointer (&self->font_name, g_free);
    g_clear_object (&self->file);

    G_OBJECT_CLASS (font_view_model_item_parent_class)->finalize (obj);
}

static void
font_view_model_item_class_init (FontViewModelItemClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);
    oclass->finalize = font_view_model_item_finalize;
}

static void
font_view_model_item_init (FontViewModelItem *self)
{
}

static FontViewModelItem *
font_view_model_item_new (const gchar *font_name,
                          GFile       *file,
                          int          face_index)
{
    FontViewModelItem *item = g_object_new (FONT_VIEW_TYPE_MODEL_ITEM, NULL);

    item->collation_key = g_utf8_collate_key (font_name, -1);
    item->font_name = g_strdup (font_name);
    item->file = g_object_ref (file);
    item->face_index = face_index;

    return item;
}

const gchar *
font_view_model_item_get_collation_key (FontViewModelItem *self)
{
    return self->collation_key;
}

const gchar *
font_view_model_item_get_font_name (FontViewModelItem *self)
{
    return self->font_name;
}

GFile *
font_view_model_item_get_font_file (FontViewModelItem *self)
{
    return self->file;
}

gint
font_view_model_item_get_face_index (FontViewModelItem *self)
{
    return self->face_index;
}

gboolean
font_view_model_has_face (FontViewModel *self,
                          FT_Face face)
{
    guint n_items;
    gint idx;
    g_autofree gchar *match_name = NULL;

    n_items = g_list_model_get_n_items (G_LIST_MODEL (self->model));
    match_name = font_utils_get_font_name (face);

    for (idx = 0; idx < n_items; idx++) {
        FontViewModelItem *item = g_list_model_get_item (G_LIST_MODEL (self->model), idx);

        if (g_strcmp0 (item->font_name, match_name) == 0)
            return TRUE;
    }

    return FALSE;
}

static void
font_infos_loaded (GObject *source_object,
                   GAsyncResult *result,
                   gpointer user_data)
{
    FontViewModel *self = FONT_VIEW_MODEL (source_object);
    g_autoptr(GPtrArray) items = g_task_propagate_pointer (G_TASK (result), NULL);

    g_list_store_splice (self->model, 0, 0, items->pdata, items->len);
}

static void
load_font_infos (GTask *task,
                 gpointer source_object,
                 gpointer user_data,
                 GCancellable *cancellable)
{
    FontViewModel *self = FONT_VIEW_MODEL (source_object);
    g_autoptr(GPtrArray) items = NULL;
    gint i, n_fonts;

    n_fonts = self->font_list->nfont;
    items = g_ptr_array_new_full (n_fonts, g_object_unref);

    for (i = 0; i < n_fonts; i++) {
        FontViewModelItem *item;
        FcChar8 *path;
        int index;
        g_autofree gchar *font_name = NULL;
        g_autoptr(GFile) file = NULL;

        if (g_task_return_error_if_cancelled (task))
            return;

        g_mutex_lock (&self->font_list_mutex);
        FcPatternGetString (self->font_list->fonts[i], FC_FILE, 0, &path);
        FcPatternGetInteger (self->font_list->fonts[i], FC_INDEX, 0, &index);
        g_mutex_unlock (&self->font_list_mutex);

        file = g_file_new_for_path ((const gchar *) path);
        font_name = font_utils_get_font_name_for_file (self->library, file, index);
        if (!font_name)
            continue;

        item = font_view_model_item_new (font_name, file, index);
        g_ptr_array_add (items, item);
    }

    g_task_return_pointer (task, g_steal_pointer (&items), NULL);
}

/* make sure the font list is valid */
static void
ensure_font_list (FontViewModel *self)
{
    FcPattern *pat;
    FcObjectSet *os;
    g_autoptr(GTask) task = NULL;

    /* always reinitialize the font database */
    if (!FcInitReinitialize())
        return;

    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);

    g_list_store_remove_all (self->model);

    pat = FcPatternCreate ();
    os = FcObjectSetBuild (FC_FILE, FC_INDEX, FC_FAMILY, FC_WEIGHT, FC_SLANT, NULL);

    g_mutex_lock (&self->font_list_mutex);

    g_clear_pointer (&self->font_list, FcFontSetDestroy);

    FcPatternAddBool (pat, FC_SCALABLE, FcTrue);
    self->font_list = FcFontList (NULL, pat, os);

    g_mutex_unlock (&self->font_list_mutex);

    FcPatternDestroy (pat);
    FcObjectSetDestroy (os);

    if (!self->font_list)
        return;

    self->cancellable = g_cancellable_new ();

    task = g_task_new (self, self->cancellable, font_infos_loaded, NULL);
    g_task_set_return_on_cancel (task, TRUE);
    g_task_run_in_thread (task, load_font_infos);
}

static gboolean
ensure_font_list_idle (gpointer user_data)
{
    FontViewModel *self = user_data;

    self->font_list_idle_id = 0;
    ensure_font_list (self);

    return FALSE;
}

static void
schedule_update_font_list (FontViewModel *self)
{
    if (self->font_list_idle_id != 0)
        return;

    self->font_list_idle_id =
        g_idle_add (ensure_font_list_idle, self);
}

static void
connect_to_fontconfig_updates (FontViewModel *self)
{
    GtkSettings *settings;

    settings = gtk_settings_get_default ();
    self->fontconfig_update_id =
        g_signal_connect_swapped (settings, "notify::gtk-fontconfig-timestamp",
                                  G_CALLBACK (ensure_font_list), self);
}

static void
font_view_model_init (FontViewModel *self)
{
    if (FT_Init_FreeType (&self->library) != FT_Err_Ok)
        g_critical ("Can't initialize FreeType library");

    g_mutex_init (&self->font_list_mutex);
    self->model = g_list_store_new (FONT_VIEW_TYPE_MODEL_ITEM);

    schedule_update_font_list (self);
    connect_to_fontconfig_updates (self);
}

static void
font_view_model_finalize (GObject *obj)
{
    FontViewModel *self = FONT_VIEW_MODEL (obj);
    GtkSettings *settings;

    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);

    g_clear_object (&self->model);
    g_clear_pointer (&self->font_list, FcFontSetDestroy);
    g_clear_pointer (&self->library, FT_Done_FreeType);

    g_clear_handle_id (&self->font_list_idle_id, g_source_remove);

    if (self->fontconfig_update_id != 0) {
        settings = gtk_settings_get_default ();
        g_signal_handler_disconnect (settings, self->fontconfig_update_id);
        self->fontconfig_update_id = 0;
    }

    g_mutex_clear (&self->font_list_mutex);

    G_OBJECT_CLASS (font_view_model_parent_class)->finalize (obj);
}

static void
font_view_model_class_init (FontViewModelClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);
    oclass->finalize = font_view_model_finalize;
}

FontViewModel *
font_view_model_new (void)
{
    return g_object_new (FONT_VIEW_TYPE_MODEL, NULL);
}

GListModel *
font_view_model_get_list_model (FontViewModel *self)
{
    return G_LIST_MODEL (self->model);
}
