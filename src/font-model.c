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

#include <ft2build.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <pango/pango.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>

#include "font-model.h"
#include "sushi-font-loader.h"

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

    gchar *font_name;
    gchar *font_preview_text;
    PangoFontDescription *font_description;
    PangoAttrList *attr_list;
    GFile *file;
    int face_index;
};

enum {
  PROP_0,
  PROP_FONT_NAME,
  PROP_FONT_PREVIEW_TEXT,
  PROP_ATTR_LIST,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

G_DEFINE_TYPE (FontViewModelItem, font_view_model_item, G_TYPE_OBJECT)

static void
font_view_model_item_finalize (GObject *obj)
{
    FontViewModelItem *self = FONT_VIEW_MODEL_ITEM (obj);

    g_clear_pointer (&self->font_name, g_free);
    g_clear_pointer (&self->font_preview_text, g_free);
    g_clear_pointer (&self->attr_list, pango_attr_list_unref);
    g_clear_object (&self->file);

    G_OBJECT_CLASS (font_view_model_item_parent_class)->finalize (obj);
}

static void
font_view_model_item_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    FontViewModelItem *self = FONT_VIEW_MODEL_ITEM (object);

    switch (prop_id) {
        case PROP_FONT_NAME:
            g_value_set_string (value, font_view_model_item_get_font_name (self));
            break;
        case PROP_FONT_PREVIEW_TEXT:
            g_value_set_string (value, font_view_model_item_get_font_preview_text (self));
            break;
        case PROP_ATTR_LIST:
            g_value_set_boxed (value, font_view_model_item_get_attribute_list (self));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
font_view_model_item_class_init (FontViewModelItemClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);
    oclass->finalize = font_view_model_item_finalize;
    oclass->get_property = font_view_model_item_get_property;

    properties[PROP_FONT_NAME] =
        g_param_spec_string ("font-name",
                             "", "", "",
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    properties[PROP_FONT_PREVIEW_TEXT] =
        g_param_spec_string ("preview-text",
                             "", "", "",
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    properties[PROP_ATTR_LIST] =
        g_param_spec_boxed ("attr-list",
                            "", "",
                            PANGO_TYPE_ATTR_LIST,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (oclass, N_PROPS, properties);
}

static void
font_view_model_item_init (FontViewModelItem *self)
{
}

static FontViewModelItem *
font_view_model_item_new (const gchar *font_name,
                          const gchar *font_preview_text,
                          PangoFontDescription *font_description,
                          GFile *file,
                          int face_index)
{
    FontViewModelItem *item = g_object_new (FONT_VIEW_TYPE_MODEL_ITEM, NULL);
    PangoAttrList *list = pango_attr_list_new ();
    PangoAttribute *attr = pango_attr_font_desc_new (font_description);

    item->font_name = g_strdup (font_name);
    item->font_preview_text = g_strdup (font_preview_text);

    pango_attr_list_insert (list, attr);
    item->attr_list = list;

    item->file = g_object_ref (file);
    item->face_index = face_index;

    return item;
}

const gchar *
font_view_model_item_get_font_name (FontViewModelItem *self)
{
    return self->font_name;
}

const gchar *
font_view_model_item_get_font_preview_text (FontViewModelItem *self)
{
    return self->font_preview_text;
}

PangoAttrList *
font_view_model_item_get_attribute_list (FontViewModelItem *item)
{
    return item->attr_list;
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
font_view_model_has_face (FontViewModel *self, FT_Face face)
{
    guint n_items;
    gint idx;
    g_autofree gchar *match_name = NULL;

    n_items = g_list_model_get_n_items (G_LIST_MODEL (self->model));
    match_name = sushi_get_font_name (face, TRUE);

    for (idx = 0; idx < n_items; idx++) {
        g_autoptr (FontViewModelItem) item =
            g_list_model_get_item (G_LIST_MODEL (self->model), idx);

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
    g_autoptr (GPtrArray) items = NULL;
    g_autoptr (GError) err = NULL;

    items = g_task_propagate_pointer (G_TASK (result), &err);

    if (err == NULL)
        g_list_store_splice (self->model, 0, 0, items->pdata, items->len);
    else if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_debug ("Loading fonts canceled");
    else
        g_warning ("Could not load font infos: %s", err->message);
}

static const gchar *
weight_to_name (int weight)
{
    switch (weight) {
    case FC_WEIGHT_THIN:
        return "Thin";
    case FC_WEIGHT_EXTRALIGHT:
        return "Extralight";
    case FC_WEIGHT_LIGHT:
        return "Light";
    case FC_WEIGHT_SEMILIGHT:
        return "Semilight";
    case FC_WEIGHT_BOOK:
        return "Book";
    case FC_WEIGHT_REGULAR:
        return "Regular";
    case FC_WEIGHT_MEDIUM:
        return "Medium";
    case FC_WEIGHT_SEMIBOLD:
        return "Semibold";
    case FC_WEIGHT_BOLD:
        return "Bold";
    case FC_WEIGHT_EXTRABOLD:
        return "Extrabold";
    case FC_WEIGHT_HEAVY:
        return "Heavy";
    case FC_WEIGHT_EXTRABLACK:
        return "Extrablack";
    }

    return NULL;
}

static const PangoWeight
fc_weight_to_pango_weight (int weight)
{
    switch (weight) {
    case FC_WEIGHT_THIN:
        return PANGO_WEIGHT_THIN;
    case FC_WEIGHT_EXTRALIGHT:
        return PANGO_WEIGHT_ULTRALIGHT;
    case FC_WEIGHT_LIGHT:
        return PANGO_WEIGHT_LIGHT;
    case FC_WEIGHT_SEMILIGHT:
        return PANGO_WEIGHT_SEMILIGHT;
    case FC_WEIGHT_BOOK:
        return PANGO_WEIGHT_BOOK;
    case FC_WEIGHT_REGULAR:
        return PANGO_WEIGHT_NORMAL;
    case FC_WEIGHT_MEDIUM:
        return PANGO_WEIGHT_MEDIUM;
    case FC_WEIGHT_SEMIBOLD:
        return PANGO_WEIGHT_SEMIBOLD;
    case FC_WEIGHT_BOLD:
        return PANGO_WEIGHT_BOLD;
    case FC_WEIGHT_EXTRABOLD:
        return PANGO_WEIGHT_ULTRABOLD;
    case FC_WEIGHT_HEAVY:
        return PANGO_WEIGHT_HEAVY;
    case FC_WEIGHT_EXTRABLACK:
        return PANGO_WEIGHT_ULTRAHEAVY;
    }

    return PANGO_WEIGHT_NORMAL;
}

static const gchar *
slant_to_name (int slant)
{
    switch (slant) {
    case FC_SLANT_ROMAN:
        return NULL;
    case FC_SLANT_ITALIC:
        return "Italic";
    case FC_SLANT_OBLIQUE:
        return "Oblique";
    }

    return NULL;
}

static const PangoStyle
fc_slant_to_pango_style (int slant)
{
    switch (slant) {
    case FC_SLANT_ITALIC:
        return PANGO_STYLE_ITALIC;
    case FC_SLANT_OBLIQUE:
        return PANGO_STYLE_OBLIQUE;
    }

    return PANGO_STYLE_NORMAL;
}

static gchar *
build_font_name (const char *style_name,
                 const char *family_name,
                 int slant,
                 int weight,
                 gboolean short_form)
{
    g_autofree char *style_name_x = g_strdup (style_name);
    const gchar *slant_name = slant_to_name (slant);
    const gchar *weight_name = weight_to_name (weight);

    if (style_name_x == NULL) {
        if (slant_name != NULL && weight_name == NULL)
            style_name_x = g_strdup_printf ("%s", slant_name);
        else if (slant_name == NULL && weight_name != NULL)
            style_name_x = g_strdup_printf ("%s", weight_name);
        else if (slant_name != NULL && weight_name != NULL)
            style_name_x = g_strdup_printf ("%s %s", weight_name, slant_name);
    }

    if (family_name == NULL) {
        /* Use an empty string as the last fallback */
        return g_strdup ("");
    }

    if (style_name_x == NULL ||
        (short_form && g_strcmp0 (style_name_x, "Regular") == 0))
        return g_strdup (family_name);

    return g_strconcat (family_name, ", ", style_name_x, NULL);
}

static void
load_font_infos (GTask *task,
                 gpointer source_object,
                 gpointer user_data,
                 GCancellable *cancellable)
{
    FontViewModel *self = FONT_VIEW_MODEL (source_object);
    g_autoptr (GPtrArray) items = NULL;
    gint i, n_fonts;

    n_fonts = self->font_list->nfont;
    items = g_ptr_array_new_full (n_fonts, g_object_unref);

    for (i = 0; i < n_fonts; i++) {
        FontViewModelItem *item;
        FcChar8 *path, *family, *style;
        int index, slant, weight;
        g_autofree gchar *font_name = NULL;
        g_autoptr (GFile) file = NULL;

        if (g_task_return_error_if_cancelled (task))
            return;

        g_mutex_lock (&self->font_list_mutex);
        FcPattern *font = self->font_list->fonts[i];
        FcPatternGetString (font, FC_FILE, 0, &path);
        FcPatternGetInteger (font, FC_INDEX, 0, &index);
        g_autofree gchar *style_name = NULL;
        g_autofree gchar *family_name = NULL;
        FcResult result = FcPatternGetString (font, FC_FAMILY, 0, &family);
        if (result == FcResultMatch) {
            family_name = g_strdup ((const gchar *) family);
        }
        result = FcPatternGetString (font, FC_STYLE, 0, &style);
        if (result == FcResultMatch) {
            style_name = g_strdup ((const gchar *) style);
        }
        result = FcPatternGetInteger (font, FC_SLANT, 0, &slant);
        if (result != FcResultMatch) {
            slant = -1;
        }
        result = FcPatternGetInteger (font, FC_WEIGHT, 0, &weight);
        if (result != FcResultMatch) {
            weight = -1;
        }
        g_mutex_unlock (&self->font_list_mutex);

        file = g_file_new_for_path ((const gchar *) path);

        font_name =
            build_font_name (style_name, family_name, slant, weight, TRUE);
        if (!font_name)
            continue;

        // TODO: Support scripts which don't contain "Aa"
        const char *font_preview_text = "Aa";

        PangoFontDescription *font_description = pango_font_description_new ();

        pango_font_description_set_family (font_description, family_name);
        pango_font_description_set_weight (font_description,
                                           fc_weight_to_pango_weight (weight));
        pango_font_description_set_style (font_description,
                                          fc_slant_to_pango_style (slant));

        item = font_view_model_item_new (font_name, font_preview_text,
                                         font_description, file, index);
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
    g_autoptr (GTask) task = NULL;

    /* always reinitialize the font database */
    if (!FcInitReinitialize ())
        return;

    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);

    g_list_store_remove_all (self->model);

    pat = FcPatternCreate ();
    os = FcObjectSetBuild (FC_FILE, FC_INDEX, FC_FAMILY, FC_WEIGHT, FC_SLANT,
                           NULL);

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

    self->font_list_idle_id = g_idle_add (ensure_font_list_idle, self);
}

static void
connect_to_fontconfig_updates (FontViewModel *self)
{
    GtkSettings *settings;

    settings = gtk_settings_get_default ();
    g_signal_connect_object (settings, "notify::gtk-fontconfig-timestamp",
                             G_CALLBACK (ensure_font_list), self,
                             G_CONNECT_SWAPPED);
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
