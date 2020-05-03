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
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libhandy-1/handy.h>
#include <hb.h>
#include <hb-ot.h>
#include <hb-ft.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-desktop-thumbnail.h>

#include "font-model.h"
#include "sushi-font-widget.h"

#define FONT_VIEW_TYPE_APPLICATION (font_view_application_get_type ())
#define FONT_VIEW_ICON_NAME APPLICATION_ID

G_DECLARE_FINAL_TYPE (FontViewApplication, font_view_application,
                      FONT_VIEW, APPLICATION,
                      GtkApplication)

struct _FontViewApplication {
    GtkApplication parent;

    GtkWidget *main_window;
    GtkWidget *main_grid;
    GtkWidget *header;
    GtkWidget *title_label;
    GtkWidget *side_grid;
    GtkWidget *font_widget;
    GtkWidget *info_button;
    GtkWidget *install_button;
    GtkWidget *install_label;
    GtkWidget *installing_label;
    GtkWidget *installed_label;
    GtkWidget *install_failed_label;
    GtkWidget *back_button;
    GtkWidget *stack;
    GtkWidget *swin_view;
    GtkWidget *swin_preview;
    GtkWidget *swin_info;
    GtkWidget *flow_box;
    GtkWidget *search_bar;
    GtkWidget *search_entry;
    GtkWidget *search_toggle;
    GtkWidget *menu_button;

    FontViewModel *model;

    GFile *font_file;

    GCancellable *cancellable;
};

G_DEFINE_TYPE (FontViewApplication, font_view_application,
               GTK_TYPE_APPLICATION);

G_DECLARE_FINAL_TYPE (FontViewItem, font_view_item,
                      FONT_VIEW, ITEM,
                      GtkFlowBoxChild);

struct _FontViewItem {
    GtkFlowBoxChild parent;

    GtkWidget *image;
    GtkWidget *label;
    FontViewModelItem *item;

    GCancellable *thumbnail_cancellable;
};

#define FONT_VIEW_TYPE_ITEM (font_view_item_get_type ())
G_DEFINE_TYPE (FontViewItem, font_view_item, GTK_TYPE_FLOW_BOX_CHILD)

static cairo_surface_t *
load_fallback_icon (gint scale_factor)
{
    static cairo_surface_t *fallback_icon = NULL;
    g_autoptr(GIcon) icon = NULL;
    g_autoptr(GtkIconInfo) icon_info = NULL;
    GtkIconTheme *icon_theme;
    const char *mimetype = "font/ttf";

    if (fallback_icon != NULL)
        return fallback_icon;

    icon_theme = gtk_icon_theme_get_default ();
    icon = g_content_type_get_icon (mimetype);
    icon_info = gtk_icon_theme_lookup_by_gicon_for_scale (icon_theme, icon,
                                                          128, scale_factor,
                                                          GTK_ICON_LOOKUP_FORCE_SIZE);
    if (!icon_info) {
        g_warning ("Fallback icon for %s not found", mimetype);
        return NULL;
    }

    fallback_icon = gtk_icon_info_load_surface (icon_info, NULL, NULL);
    return fallback_icon;
}

static void
font_view_item_dispose (GObject *obj)
{
    FontViewItem *self = FONT_VIEW_ITEM (obj);

    g_clear_object (&self->item);
    g_cancellable_cancel (self->thumbnail_cancellable);
    g_clear_object (&self->thumbnail_cancellable);

    G_OBJECT_CLASS (font_view_item_parent_class)->dispose (obj);
}

static void
font_view_item_class_init (FontViewItemClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);
    oclass->dispose = font_view_item_dispose;
}

static void
font_view_item_init (FontViewItem *self)
{
    GtkWidget *box;
    gint scale_factor = gtk_widget_get_scale_factor (GTK_WIDGET (self));

    box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add (GTK_CONTAINER (self), box);

    self->image = gtk_image_new ();
    gtk_widget_set_margin_start (self->image, 6);
    gtk_widget_set_margin_end (self->image, 6);
    gtk_widget_set_halign (self->image, GTK_ALIGN_CENTER);
    gtk_image_set_from_surface (GTK_IMAGE (self->image), load_fallback_icon (scale_factor));
    gtk_container_add (GTK_CONTAINER (box), self->image);

    self->label = gtk_label_new (NULL);
    gtk_widget_set_halign (self->label, GTK_ALIGN_CENTER);
    gtk_label_set_line_wrap (GTK_LABEL (self->label), TRUE);
    gtk_label_set_line_wrap_mode (GTK_LABEL (self->label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars (GTK_LABEL (self->label), 18);
    gtk_label_set_justify (GTK_LABEL (self->label), GTK_JUSTIFY_CENTER);
    gtk_container_add (GTK_CONTAINER (box), self->label);
}

#define ATTRIBUTES_FOR_CREATING_THUMBNAIL \
    G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE"," \
    G_FILE_ATTRIBUTE_TIME_MODIFIED
#define ATTRIBUTES_FOR_EXISTING_THUMBNAIL \
    G_FILE_ATTRIBUTE_THUMBNAIL_PATH"," \
    G_FILE_ATTRIBUTE_THUMBNAILING_FAILED

static GdkPixbuf *
create_thumbnail (GFile       *font_file,
                  const gchar *thumb_uri,
                  gint         scale_factor)
{
    g_autoptr(GdkPixbuf) pixbuf = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GFileInfo) info = NULL;
    g_autoptr(GnomeDesktopThumbnailFactory) factory = NULL;
    guint64 mtime;

    info = g_file_query_info (font_file, ATTRIBUTES_FOR_CREATING_THUMBNAIL,
                              G_FILE_QUERY_INFO_NONE,
                              NULL, &error);

    /* we don't care about reporting errors here, just fail the
     * thumbnail.
     */
    if (info == NULL) {
        g_autofree gchar *file_uri = g_file_get_uri (font_file);
        g_debug ("Can't query info for file %s: %s", file_uri, error->message);
        return NULL;
    }

    factory = gnome_desktop_thumbnail_factory_new (GNOME_DESKTOP_THUMBNAIL_SIZE_LARGE);
    pixbuf = gnome_desktop_thumbnail_factory_generate_thumbnail
        (factory, thumb_uri, g_file_info_get_content_type (info));

    mtime = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
    if (pixbuf != NULL) {
        GdkPixbuf *scaled = gdk_pixbuf_scale_simple (pixbuf,
                                                     128 * scale_factor,
                                                     128 * scale_factor,
                                                     GDK_INTERP_BILINEAR);
        gnome_desktop_thumbnail_factory_save_thumbnail (factory, pixbuf,
                                                        thumb_uri, (time_t) mtime);
        g_object_unref (pixbuf);
        pixbuf = scaled;
    } else {
        gnome_desktop_thumbnail_factory_create_failed_thumbnail (factory,
                                                                 thumb_uri, (time_t) mtime);
    }

    return g_steal_pointer (&pixbuf);
}

static void
font_view_item_load_thumbnail_job (GTask *task,
                                   gpointer source_object,
                                   gpointer task_data,
                                   GCancellable *cancellable)
{
    FontViewItem *self = source_object;
    FontViewModelItem *item = self->item;
    gint scale_factor = GPOINTER_TO_INT (task_data);
    g_autoptr(GdkPixbuf) pixbuf = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *thumb_path = NULL, *thumb_uri = NULL, *file_uri = NULL;
    gint face_index;
    GFile *font_file;

    if (g_task_return_error_if_cancelled (task))
        return;

    face_index = font_view_model_item_get_face_index (item);
    font_file = font_view_model_item_get_font_file (item);
    file_uri = g_file_get_uri (font_file);

    if (face_index == 0) {
        g_autoptr(GFileInfo) info = NULL;
        gboolean thumb_failed;

        thumb_uri = g_strdup (file_uri);
        info = g_file_query_info (font_file,
                                  ATTRIBUTES_FOR_EXISTING_THUMBNAIL,
                                  G_FILE_QUERY_INFO_NONE,
                                  NULL, &error);

        if (error != NULL) {
            g_debug ("Can't query info for file %s: %s", file_uri, error->message);
            return;
        }

        thumb_failed = g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_THUMBNAILING_FAILED);
        if (thumb_failed)
            return;

        thumb_path = g_strdup (g_file_info_get_attribute_byte_string (info, G_FILE_ATTRIBUTE_THUMBNAIL_PATH));
    } else {
        g_autofree gchar *checksum = NULL, *filename = NULL;

        thumb_uri = g_strdup_printf ("%s#0x%08X", file_uri, face_index);
        checksum = g_compute_checksum_for_data (G_CHECKSUM_MD5,
                                                (const guchar *) thumb_uri,
                                                strlen (thumb_uri));
        filename = g_strdup_printf ("%s.png", checksum);

        thumb_path = g_build_filename (g_get_user_cache_dir (),
                                       "thumbnails",
                                       "large",
                                       filename,
                                       NULL);

        if (!g_file_test (thumb_path, G_FILE_TEST_IS_REGULAR))
            g_clear_pointer (&thumb_path, g_free);
    }

    if (thumb_path != NULL) {
        g_autoptr(GFile) thumb_file = NULL;
        g_autoptr(GFileInputStream) is = NULL;

        thumb_file = g_file_new_for_path (thumb_path);
        is = g_file_read (thumb_file, NULL, &error);

        if (error != NULL) {
            g_debug ("Can't read file %s: %s", thumb_path, error->message);
            return;
        }

        pixbuf = gdk_pixbuf_new_from_stream_at_scale (G_INPUT_STREAM (is),
                                                      128 * scale_factor, 128 * scale_factor,
                                                      TRUE,
                                                      NULL, &error);

        if (error != NULL) {
            g_debug ("Can't read thumbnail pixbuf %s: %s", thumb_path, error->message);
            return;
        }
    } else {
        pixbuf = create_thumbnail (font_file, thumb_uri, scale_factor);
    }

    if (pixbuf != NULL)
        g_task_return_pointer (task, gdk_cairo_surface_create_from_pixbuf (pixbuf, scale_factor, NULL),
                               (GDestroyNotify) cairo_surface_destroy);
    else
        g_task_return_pointer (task, NULL, NULL);
}

static void
font_view_item_thumbnail_loaded (GObject *source_object,
                                 GAsyncResult *result,
                                 gpointer user_data)
{
    FontViewItem *self = user_data;
    cairo_surface_t *surface = g_task_propagate_pointer (G_TASK (result), NULL);

    g_clear_object (&self->thumbnail_cancellable);

    if (surface != NULL) {
        gtk_image_set_from_surface (GTK_IMAGE (self->image), surface);
        cairo_surface_destroy (surface);
    }
}

static void
font_view_item_load_thumbnail (FontViewItem *self)
{
    g_autoptr(GTask) task = NULL;
    gint scale_factor = gtk_widget_get_scale_factor (GTK_WIDGET (self));

    self->thumbnail_cancellable = g_cancellable_new ();
    task = g_task_new (self, self->thumbnail_cancellable,
                       font_view_item_thumbnail_loaded, self);
    g_task_set_task_data (task, GINT_TO_POINTER (scale_factor), NULL);
    g_task_run_in_thread (task, font_view_item_load_thumbnail_job);
}

static GtkWidget *
font_view_item_new (FontViewModelItem *item)
{
    FontViewItem *view_item = g_object_new (FONT_VIEW_TYPE_ITEM, NULL);

    view_item->item = g_object_ref (item);
    gtk_label_set_text (GTK_LABEL (view_item->label),
                        font_view_model_item_get_font_name (item));
    font_view_item_load_thumbnail (view_item);
    gtk_widget_show_all (GTK_WIDGET (view_item));

    return GTK_WIDGET (view_item);
}

static void font_view_application_do_overview (FontViewApplication *self);
static void ensure_window (FontViewApplication *self);

#define VIEW_COLUMN_SPACING 18
#define VIEW_MARGIN 16

static gboolean
_print_version_and_exit (const gchar *option_name,
                         const gchar *value,
                         gpointer data,
                         GError **error)
{
    g_print("%s %s\n", _("GNOME Fonts"), VERSION);
    exit (EXIT_SUCCESS);
    return TRUE;
}

static const GOptionEntry goption_options[] =
{
    { "version", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
      _print_version_and_exit, N_("Show the application's version"), NULL},
    { NULL }
};

#define WHITESPACE_CHARS "\f \t"

static void
strip_whitespace (gchar **original)
{
    g_auto(GStrv) split = NULL;
    g_autoptr(GString) reassembled = NULL;
    const gchar *str;
    gint idx, n_stripped;
    size_t len;

    split = g_strsplit (*original, "\n", -1);
    reassembled = g_string_new (NULL);
    n_stripped = 0;

    for (idx = 0; split[idx] != NULL; idx++) {
        str = split[idx];

        len = strspn (str, WHITESPACE_CHARS);
        if (len)
            str += len;

        if (strlen (str) == 0 &&
            ((split[idx + 1] == NULL) || strlen (split[idx + 1]) == 0))
            continue;

        if (n_stripped++ > 0)
            g_string_append (reassembled, "\n");
        g_string_append (reassembled, str);
    }

    g_free (*original);
    *original = g_strdup (reassembled->str);
}

#define MATCH_VERSION_STR "Version"

static void
strip_version (gchar **original)
{
    gchar *ptr, *stripped;

    ptr = g_strstr_len (*original, -1, MATCH_VERSION_STR);
    if (!ptr)
        return;

    ptr += strlen (MATCH_VERSION_STR);
    stripped = g_strdup (ptr);

    strip_whitespace (&stripped);

    g_free (*original);
    *original = stripped;
}

static void
add_row (GtkWidget *grid,
         const gchar *name,
         const gchar *value,
         gboolean multiline)
{
    GtkWidget *name_w, *label;
    int i;
    const char *p;

    name_w = gtk_label_new (name);
    gtk_style_context_add_class (gtk_widget_get_style_context (name_w), "dim-label");
    gtk_widget_set_halign (name_w, GTK_ALIGN_END);
    gtk_widget_set_valign (name_w, GTK_ALIGN_START);

    gtk_container_add (GTK_CONTAINER (grid), name_w);

    label = gtk_label_new (value);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_widget_set_valign (label, GTK_ALIGN_START);
    gtk_label_set_selectable (GTK_LABEL(label), TRUE);

    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);

    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 64);

    if (multiline && g_utf8_strlen (value, -1) > 64) {
        gtk_label_set_width_chars (GTK_LABEL (label), 64);
        gtk_label_set_lines (GTK_LABEL (label), 10);

        p = value;
        i = 0;
        while (p) {
            p = strchr (p + 1, '\n');
            i++;
        }
        if (i > 3) { /* multi-paragraph text */
            gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_NONE);
            gtk_label_set_lines (GTK_LABEL (label), -1);
        }
    }

    gtk_grid_attach_next_to (GTK_GRID (grid), label,
                             name_w, GTK_POS_RIGHT,
                             1, 1);
}

#define FixedToFloat(f) (((float)(f))/65536.0)

static char *
describe_axis (FT_Var_Axis *ax)
{
  /* Translators, this string is used to display information about
   * a 'font variation axis'. The %s gets replaced with the name
   * of the axis, for example 'Width'. The three %g get replaced
   * with the minimum, maximum and default values for the axis.
   */
  return g_strdup_printf (_("%s %g — %g, default %g"), ax->name,
                          FixedToFloat (ax->minimum),
                          FixedToFloat (ax->maximum),
                          FixedToFloat (ax->def));
}

static char *
get_sfnt_name (FT_Face face,
               guint id)
{
    guint count, i;

    count = FT_Get_Sfnt_Name_Count (face);
    for (i = 0; i < count; i++) {
        FT_SfntName sname;

        if (FT_Get_Sfnt_Name (face, i, &sname) != 0)
            continue;

        if (sname.name_id != id)
            continue;

        /* only handle the unicode names for US langid */
        if (!(sname.platform_id == TT_PLATFORM_MICROSOFT &&
            sname.encoding_id == TT_MS_ID_UNICODE_CS &&
            sname.language_id == TT_MS_LANGID_ENGLISH_UNITED_STATES))
            continue;

        return g_convert ((gchar *)sname.string, sname.string_len,
                          "UTF-8", "UTF-16BE", NULL, NULL, NULL);
    }
    return NULL;
}

/* According to the OpenType spec, valid values for the subfamilyId field
 * of InstanceRecords are 2, 17 or values in the range (255,32768). See
 * https://www.microsoft.com/typography/otspec/fvar.htm#instanceRecord
 */
static gboolean
is_valid_subfamily_id (guint id)
{
  return id == 2 || id == 17 || (255 < id && id < 32768);
}

static void
describe_instance (FT_Face face,
                   FT_Var_Named_Style *ns,
                   int pos,
                   GString *s)
{
    g_autofree char *str = NULL;

    if (is_valid_subfamily_id (ns->strid))
        str = get_sfnt_name (face, ns->strid);

    if (str == NULL)
        str = g_strdup_printf (_("Instance %d"), pos);

    if (s->len > 0)
        g_string_append (s, ", ");
    g_string_append (s, str);
}

#include "open-type-layout.h"

static char *
get_features (FT_Face face)
{
    g_autoptr(GString) s = NULL;
    hb_font_t *hb_font;
    int i, j, k;

    s = g_string_new ("");

    hb_font = hb_ft_font_create (face, NULL);
    if (hb_font) {
        hb_tag_t tables[2] = { HB_OT_TAG_GSUB, HB_OT_TAG_GPOS };
        hb_face_t *hb_face;

        hb_face = hb_font_get_face (hb_font);

        for (i = 0; i < 2; i++) {
            hb_tag_t features[80];
            unsigned int count = G_N_ELEMENTS (features);
            unsigned int script_index = 0;
            unsigned int lang_index = HB_OT_LAYOUT_DEFAULT_LANGUAGE_INDEX;

            hb_ot_layout_language_get_feature_tags (hb_face,
                                                    tables[i],
                                                    script_index,
                                                    lang_index,
                                                    0,
                                                    &count,
                                                    features);
            for (j = 0; j < count; j++) {
                for (k = 0; k < G_N_ELEMENTS (open_type_layout_features); k++) {
                    if (open_type_layout_features[k].tag == features[j]) {
                        if (s->len > 0)
                            /* Translators, this seperates the list of Layout Features. */
                            g_string_append (s, C_("OpenType layout", ", "));
                        g_string_append (s, g_dpgettext2 (NULL, "OpenType layout", open_type_layout_features[k].name));
                        break;
                    }
                }
            }
        }
    }

    if (s->len > 0)
        return g_strdup (s->str);

    return NULL;
}

static void
populate_grid (FontViewApplication *self,
               GtkWidget *grid,
               FT_Face face)
{
    g_autoptr (GFileInfo) info = NULL;
    g_autofree gchar *path = NULL;
    PS_FontInfoRec ps_info;

    add_row (grid, _("Name"), face->family_name, FALSE);

    path = g_file_get_path (self->font_file);
    add_row (grid, _("Location"), path, FALSE);

    if (face->style_name)
        add_row (grid, _("Style"), face->style_name, FALSE);

    info = g_file_query_info (self->font_file,
                              G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
                              G_FILE_ATTRIBUTE_STANDARD_SIZE,
                              G_FILE_QUERY_INFO_NONE,
                              NULL, NULL);

    if (info != NULL) {
        g_autofree gchar *s = g_content_type_get_description (g_file_info_get_content_type (info));
        add_row (grid, _("Type"), s, FALSE);
    }

    if (FT_IS_SFNT (face)) {
        gint i, len;
        g_autofree gchar *version = NULL, *copyright = NULL, *description = NULL;
        g_autofree gchar *designer = NULL, *manufacturer = NULL, *license = NULL;

        len = FT_Get_Sfnt_Name_Count (face);
        for (i = 0; i < len; i++) {
            FT_SfntName sname;

            if (FT_Get_Sfnt_Name (face, i, &sname) != 0)
                continue;

            /* only handle the unicode names for US langid */
            if (!(sname.platform_id == TT_PLATFORM_MICROSOFT &&
                  sname.encoding_id == TT_MS_ID_UNICODE_CS &&
                  sname.language_id == TT_MS_LANGID_ENGLISH_UNITED_STATES))
                continue;

            switch (sname.name_id) {
            case TT_NAME_ID_COPYRIGHT:
                if (!copyright)
                    copyright = g_convert ((gchar *)sname.string, sname.string_len,
                                           "UTF-8", "UTF-16BE", NULL, NULL, NULL);
                break;
            case TT_NAME_ID_VERSION_STRING:
                if (!version)
                    version = g_convert ((gchar *)sname.string, sname.string_len,
                                         "UTF-8", "UTF-16BE", NULL, NULL, NULL);
                break;
            case TT_NAME_ID_DESCRIPTION:
                if (!description)
                    description = g_convert ((gchar *)sname.string, sname.string_len,
                                             "UTF-8", "UTF-16BE", NULL, NULL, NULL);
                break;
            case TT_NAME_ID_MANUFACTURER:
                if (!manufacturer)
                    manufacturer = g_convert ((gchar *)sname.string, sname.string_len,
                                              "UTF-8", "UTF-16BE", NULL, NULL, NULL);
                break;
            case TT_NAME_ID_DESIGNER:
                if (!designer)
                    designer = g_convert ((gchar *)sname.string, sname.string_len,
                                          "UTF-8", "UTF-16BE", NULL, NULL, NULL);
                break;
            case TT_NAME_ID_LICENSE:
                if (!license)
                    license = g_convert ((gchar *)sname.string, sname.string_len,
                                         "UTF-8", "UTF-16BE", NULL, NULL, NULL);
                break;
            default:
                break;
            }
        }
        if (version) {
            strip_version (&version);
            add_row (grid, _("Version"), version, FALSE);
        }
        if (copyright) {
            strip_whitespace (&copyright);
            add_row (grid, _("Copyright"), copyright, TRUE);
        }
        if (description) {
            strip_whitespace (&description);
            add_row (grid, _("Description"), description, TRUE);
        }
        if (manufacturer) {
            strip_whitespace (&manufacturer);
            add_row (grid, _("Manufacturer"), manufacturer, TRUE);
        }
        if (designer) {
            strip_whitespace (&designer);
            add_row (grid, _("Designer"), designer, TRUE);
        }
        if (license) {
            strip_whitespace (&license);
            add_row (grid, _("License"), license, TRUE);
        }
    } else if (FT_Get_PS_Font_Info (face, &ps_info) == 0) {
        if (ps_info.version && g_utf8_validate (ps_info.version, -1, NULL)) {
            g_autofree gchar *compressed = g_strcompress (ps_info.version);
            strip_version (&compressed);
            add_row (grid, _("Version"), compressed, FALSE);
        }
        if (ps_info.notice && g_utf8_validate (ps_info.notice, -1, NULL)) {
            g_autofree gchar *compressed = g_strcompress (ps_info.notice);
            strip_whitespace (&compressed);
            add_row (grid, _("Copyright"), compressed, TRUE);
        }
    }
}

static void
populate_details (FontViewApplication *self,
                  GtkWidget *grid,
                  FT_Face face)
{
    g_autofree gchar *glyph_count = NULL, *features = NULL;
    FT_MM_Var *ft_mm_var;

    glyph_count = g_strdup_printf ("%ld", face->num_glyphs);
    add_row (grid, _("Glyph Count"), glyph_count, FALSE);

    add_row (grid, _("Color Glyphs"), FT_HAS_COLOR (face) ? _("yes") : _("no"), FALSE);

    features = get_features (face);
    if (features)
        add_row (grid, _("Layout Features"), features, TRUE);

    if (FT_Get_MM_Var (face, &ft_mm_var) == 0) {
        int i;
        for (i = 0; i < ft_mm_var->num_axis; i++) {
            g_autofree gchar *s = describe_axis (&ft_mm_var->axis[i]);
            add_row (grid, i == 0 ? _("Variation Axes") : "", s, FALSE);
        }
        {
            g_autoptr(GString) str = g_string_new ("");
            for (i = 0; i < ft_mm_var->num_namedstyles; i++)
                describe_instance (face, &ft_mm_var->namedstyle[i], i, str);

            add_row (grid, _("Named Styles"), str->str, TRUE);
        }
        free (ft_mm_var);
    }
}

static void
install_button_refresh_appearance (FontViewApplication *self,
                                   GError *error)
{
    FT_Face face;
    GtkStyleContext *context;
    GtkWidget *current_label;

    context = gtk_widget_get_style_context (self->install_button);

    current_label = gtk_bin_get_child (GTK_BIN (self->install_button));
    gtk_container_remove (GTK_CONTAINER (self->install_button), current_label);

    if (error != NULL) {
        gtk_container_add (GTK_CONTAINER (self->install_button), self->install_failed_label);
        gtk_widget_set_sensitive (self->install_button, FALSE);
        gtk_style_context_remove_class (context, "suggested-action");
    } else {
        face = sushi_font_widget_get_ft_face (SUSHI_FONT_WIDGET (self->font_widget));

        if (font_view_model_has_face (FONT_VIEW_MODEL (self->model), face)) {
            gtk_container_add (GTK_CONTAINER (self->install_button), self->installed_label);
            gtk_widget_set_sensitive (self->install_button, FALSE);
            gtk_style_context_remove_class (context, "suggested-action");
        } else if (self->cancellable != NULL) {
            gtk_container_add (GTK_CONTAINER (self->install_button), self->installing_label);
            gtk_widget_set_sensitive (self->install_button, FALSE);
        } else {
            gtk_container_add (GTK_CONTAINER (self->install_button), self->install_label);
            gtk_widget_set_sensitive (self->install_button, TRUE);
            gtk_style_context_add_class (context, "suggested-action");
        }
    }
}

static void
font_view_populate_from_model (FontViewApplication *self,
                               guint position,
                               guint removed,
                               guint added)
{
    GtkFlowBox *flow_box = GTK_FLOW_BOX (self->flow_box);
    GListModel *list_model = font_view_model_get_list_model (self->model);
    gint i;

    while (removed--) {
        GtkFlowBoxChild *child;

        child = gtk_flow_box_get_child_at_index (flow_box, position);
        gtk_widget_destroy (GTK_WIDGET (child));
    }

    for (i = 0; i < added; i++) {
        g_autoptr(FontViewModelItem) item = g_list_model_get_item (list_model, position + i);
        GtkWidget *widget = font_view_item_new (item);

        gtk_flow_box_insert (flow_box, widget, position + i);
    }
}

static void
font_model_items_changed_cb (GListModel *model,
                             guint position,
                             guint removed,
                             guint added,
                             gpointer user_data)
{
    FontViewApplication *self = user_data;

    if (self->font_file != NULL)
        install_button_refresh_appearance (self, NULL);

    if (self->flow_box != NULL)
        font_view_populate_from_model (self, position, removed, added);
}

static void
font_view_show_error (FontViewApplication *self,
                      const gchar *primary_text,
                      const gchar *secondary_text)
{
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new (GTK_WINDOW (self->main_window),
                                     GTK_DIALOG_MODAL,
                                     GTK_MESSAGE_ERROR,
                                     GTK_BUTTONS_CLOSE,
                                     "%s",
                                     primary_text);
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                              "%s", secondary_text);
    g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
    gtk_widget_show (dialog);
}

static void
font_view_show_install_error (FontViewApplication *self,
                              GError *error)
{
    install_button_refresh_appearance (self, error);
    font_view_show_error (self, _("This font could not be installed."), error->message);
}

static void
font_install_finished (GObject      *source_object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
    FontViewApplication *self = user_data;
    g_autoptr(GError) err = NULL;

    g_task_propagate_boolean (G_TASK (res), &err);

    if (err != NULL)
        font_view_show_install_error (self, err);

    g_clear_object (&self->cancellable);
}

static void
install_font_job (GTask *task,
                  gpointer source_object,
                  gpointer user_data,
                  GCancellable *cancellable)
{
    GFile *dest_location = user_data;
    FontViewApplication *self = FONT_VIEW_APPLICATION (source_object);
    g_autofree gchar *dest_basename = g_file_get_basename (self->font_file);
    g_autoptr(GError) error = NULL;
    gboolean created = FALSE;
    gint i = 0;

    while (!created) {
        g_autofree gchar *dest_filename = (i == 0) ?
            g_strdup (dest_basename) : g_strdup_printf ("%d%s", i, dest_basename);
        g_autoptr(GFile) dest_file = g_file_get_child (dest_location, dest_filename);

        created = g_file_copy (self->font_file,
                               dest_file,
                               G_FILE_COPY_NONE,
                               cancellable,
                               NULL, NULL,
                               &error);

        if (error != NULL) {
            if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
                g_clear_error (&error);
                i++;
                continue;
            }
            break;
        }
    }

    if (error != NULL)
        g_task_return_error (task, g_steal_pointer (&error));
    else
        g_task_return_boolean (task, TRUE);
}

static void
font_view_install_font (FontViewApplication *self,
                        GFile *dest_location)
{
    g_autoptr(GTask) task = NULL;

    self->cancellable = g_cancellable_new ();

    task = g_task_new (self, self->cancellable, font_install_finished, self);
    g_task_set_task_data (task, g_object_ref (dest_location), g_object_unref);
    g_task_run_in_thread (task, install_font_job);
}

static void
install_button_clicked_cb (GtkButton *button,
                           gpointer user_data)
{
    FontViewApplication *self = user_data;
    g_autoptr(GError) err = NULL;
    g_autoptr(GFile) home_prefix = NULL, xdg_prefix = NULL;
    g_autoptr(GFile) xdg_location = NULL, home_location = NULL, dest_location = NULL;
    FcConfig *config;
    FcStrList *str_list;
    FcChar8 *path;

    config = FcConfigGetCurrent ();
    str_list = FcConfigGetFontDirs (config);

    home_prefix = g_file_new_for_path (g_get_home_dir ());
    xdg_prefix = g_file_new_for_path (g_get_user_data_dir ());

    /* pick the XDG location, if any, or fallback to the first location
     * under the home directory.
     */
    while ((path = FcStrListNext (str_list)) != NULL) {
        g_autoptr(GFile) file = g_file_new_for_path ((const gchar *) path);

        if (g_file_has_prefix (file, xdg_prefix)) {
            xdg_location = g_steal_pointer (&file);
            break;
        }

        if ((home_location == NULL) &&
            g_file_has_prefix (file, home_prefix)) {
            home_location = g_steal_pointer (&file);
            break;
        }
    }

    FcStrListDone (str_list);

    if (xdg_location != NULL)
        dest_location = g_steal_pointer (&xdg_location);
    else if (home_location != NULL)
        dest_location = g_steal_pointer (&home_location);

    if (dest_location == NULL) {
        g_warning ("Install failed: can't find any configured user font directory.");
        return;
    }

    if (!g_file_query_exists (dest_location, NULL)) {
        g_file_make_directory_with_parents (dest_location, NULL, &err);
        if (err) {
            font_view_show_install_error (self, err);
            return;
        }
    }

    font_view_install_font (self, dest_location);

    install_button_refresh_appearance (self, NULL);
}

static void
font_view_show_font_error (FontViewApplication *self,
                           GError *error)
{
    font_view_show_error (self, _("This font could not be displayed."), error->message);
}

static void
font_widget_error_cb (SushiFontWidget *font_widget,
                      GError *error,
                      gpointer user_data)
{
    FontViewApplication *self = user_data;

    font_view_application_do_overview (self);
    font_view_show_font_error (self, error);
}

static void
font_widget_loaded_cb (SushiFontWidget *font_widget,
                       gpointer user_data)
{
    FontViewApplication *self = user_data;
    FT_Face face = sushi_font_widget_get_ft_face (font_widget);
    const gchar *uri;

    if (face == NULL)
        return;

    uri = sushi_font_widget_get_uri (font_widget);
    self->font_file = g_file_new_for_uri (uri);

    if (face->family_name) {
        hdy_header_bar_set_title (HDY_HEADER_BAR (self->header), face->family_name);
    } else {
        g_autofree gchar *basename = g_file_get_basename (self->font_file);
        hdy_header_bar_set_title (HDY_HEADER_BAR (self->header), basename);
    }

    hdy_header_bar_set_subtitle (HDY_HEADER_BAR (self->header), face->style_name);

    install_button_refresh_appearance (self, NULL);
}

static void
info_button_clicked_cb (GtkButton *button,
                        gpointer user_data)
{
    FontViewApplication *self = user_data;
    GtkWidget *grid;
    GtkWidget *child;
    FT_Face face = sushi_font_widget_get_ft_face (SUSHI_FONT_WIDGET (self->font_widget));

    if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
        gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "preview");
        return;
    }

    if (face == NULL)
        return;

    child = gtk_bin_get_child (GTK_BIN (self->swin_info));
    if (child)
        gtk_widget_destroy (child);

    grid = gtk_grid_new ();
    gtk_orientable_set_orientation (GTK_ORIENTABLE (grid), GTK_ORIENTATION_VERTICAL);
    g_object_set (grid, "margin", 20, NULL);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 8);
    gtk_grid_set_row_spacing (GTK_GRID (grid), 2);

    populate_grid (self, grid, face);
    populate_details (self, grid, face);
    gtk_container_add (GTK_CONTAINER (self->swin_info), grid);

    gtk_widget_show_all (self->swin_info);
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "info");
}

static gboolean
font_view_filter_func (GtkFlowBoxChild *child,
                       gpointer user_data)
{
    FontViewApplication *self = user_data;
    FontViewItem *view_item = FONT_VIEW_ITEM (child);
    FontViewModelItem *item = view_item->item;
    g_autofree gchar *cf_name = NULL, *cf_search = NULL;
    const char *font_name, *search;

    if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->search_toggle)))
        return TRUE;

    search = gtk_entry_get_text (GTK_ENTRY (self->search_entry));
    font_name = font_view_model_item_get_font_name (item);

    cf_name = g_utf8_casefold (font_name, -1);
    cf_search = g_utf8_casefold (search, -1);

    return strstr (cf_name, cf_search) != NULL;
}

static gint
font_view_sort_func (GtkFlowBoxChild *child1,
                     GtkFlowBoxChild *child2,
                     gpointer user_data)
{
    FontViewModelItem *item1 = FONT_VIEW_ITEM (child1)->item;
    FontViewModelItem *item2 = FONT_VIEW_ITEM (child2)->item;

    return g_strcmp0 (font_view_model_item_get_collation_key (item1),
                      font_view_model_item_get_collation_key (item2));
}

static void
font_view_ensure_model (FontViewApplication *self)
{
    if (self->model != NULL)
        return;

    self->model = font_view_model_new ();
    g_signal_connect (font_view_model_get_list_model (self->model), "items-changed",
                      G_CALLBACK (font_model_items_changed_cb), self);
}

static void
font_view_application_do_open (FontViewApplication *self,
                               GFile *file,
                               gint face_index)
{
    g_autofree gchar *uri = NULL;

    font_view_ensure_model (self);

    /* add install button */
    if (self->install_button == NULL) {
        g_autoptr(GtkSizeGroup) install_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

        self->install_label = g_object_ref_sink (gtk_label_new (_("Install")));
        gtk_widget_show (self->install_label);
        gtk_size_group_add_widget (install_size_group, self->install_label);
        self->installing_label = g_object_ref_sink (gtk_label_new (_("Installing…")));
        gtk_widget_show (self->installing_label);
        gtk_size_group_add_widget (install_size_group, self->installing_label);
        self->installed_label = g_object_ref_sink (gtk_label_new (_("Installed")));
        gtk_widget_show (self->installed_label);
        gtk_size_group_add_widget (install_size_group, self->installed_label);
        self->install_failed_label = g_object_ref_sink (gtk_label_new (_("Failed")));
        gtk_widget_show (self->install_failed_label);
        gtk_size_group_add_widget (install_size_group, self->install_failed_label);

        self->install_button = gtk_button_new ();
        gtk_container_add (GTK_CONTAINER (self->install_button), self->install_label);
        gtk_widget_set_valign (self->install_button, GTK_ALIGN_CENTER);
        gtk_style_context_add_class (gtk_widget_get_style_context (self->install_button),
                                     "text-button");
        hdy_header_bar_pack_end (HDY_HEADER_BAR (self->header), self->install_button);

        g_signal_connect (self->install_button, "clicked",
                          G_CALLBACK (install_button_clicked_cb), self);
    }

    if (self->info_button == NULL) {
        self->info_button = gtk_toggle_button_new_with_label (_("Info"));
        gtk_widget_set_valign (self->info_button, GTK_ALIGN_CENTER);
        gtk_style_context_add_class (gtk_widget_get_style_context (self->info_button),
                                     "text-button");
        hdy_header_bar_pack_end (HDY_HEADER_BAR (self->header), self->info_button);

        g_signal_connect (self->info_button, "toggled",
                          G_CALLBACK (info_button_clicked_cb), self);
    }

    if (self->back_button == NULL) {
        GtkWidget *back_image;

        self->back_button = gtk_button_new ();
        back_image = gtk_image_new_from_icon_name ("go-previous-symbolic",
                                                   GTK_ICON_SIZE_MENU);
        gtk_button_set_image (GTK_BUTTON (self->back_button), back_image);
        gtk_widget_set_tooltip_text (self->back_button, _("Back"));
        gtk_widget_set_valign (self->back_button, GTK_ALIGN_CENTER);
        gtk_style_context_add_class (gtk_widget_get_style_context (self->back_button),
                                     "image-button");
        hdy_header_bar_pack_start (HDY_HEADER_BAR (self->header), self->back_button);

        gtk_actionable_set_action_name (GTK_ACTIONABLE (self->back_button), "app.back");
    }

    gtk_widget_hide (self->search_toggle);
    gtk_widget_hide (self->menu_button);

    uri = g_file_get_uri (file);

    if (self->font_widget == NULL) {
        GtkWidget *viewport;

        self->font_widget = GTK_WIDGET (sushi_font_widget_new (uri, face_index));
        gtk_container_add (GTK_CONTAINER (self->swin_preview), self->font_widget);
        viewport = gtk_widget_get_parent (self->font_widget);
        gtk_scrollable_set_hscroll_policy (GTK_SCROLLABLE (viewport), GTK_SCROLL_NATURAL);
        gtk_scrollable_set_vscroll_policy (GTK_SCROLLABLE (viewport), GTK_SCROLL_NATURAL);

        g_signal_connect (self->font_widget, "loaded",
                          G_CALLBACK (font_widget_loaded_cb), self);
        g_signal_connect (self->font_widget, "error",
                          G_CALLBACK (font_widget_error_cb), self);
    } else {
        g_object_set (self->font_widget, "uri", uri, "face-index", face_index, NULL);
        sushi_font_widget_load (SUSHI_FONT_WIDGET (self->font_widget));
    }

    gtk_widget_show_all (self->main_window);
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "preview");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->info_button), FALSE);
}

static void
view_child_activated_cb (GtkFlowBox *flow_box,
                         GtkFlowBoxChild *child,
                         gpointer user_data)
{
    FontViewApplication *self = user_data;
    FontViewItem *view_item = FONT_VIEW_ITEM (child);
    FontViewModelItem *item = view_item->item;
    GFile *font_file;
    gint face_index;

    font_file = font_view_model_item_get_font_file (item);
    face_index = font_view_model_item_get_face_index (item);

    if (font_file != NULL)
        font_view_application_do_open (self, font_file, face_index);
}

static void
font_view_application_do_overview (FontViewApplication *self)
{
    g_clear_object (&self->font_file);

    g_clear_pointer (&self->back_button, gtk_widget_destroy);
    g_clear_pointer (&self->info_button, gtk_widget_destroy);

    if (self->install_button) {
        g_clear_pointer (&self->install_label, gtk_widget_destroy);
        g_clear_pointer (&self->installing_label, gtk_widget_destroy);
        g_clear_pointer (&self->installed_label, gtk_widget_destroy);
        g_clear_pointer (&self->install_failed_label, gtk_widget_destroy);

        gtk_widget_destroy (self->install_button);
        self->install_button = NULL;
    }

    gtk_widget_show (self->search_toggle);
    gtk_widget_show (self->menu_button);

    font_view_ensure_model (self);

    hdy_header_bar_set_title (HDY_HEADER_BAR (self->header), _("All Fonts"));
    hdy_header_bar_set_subtitle (HDY_HEADER_BAR (self->header), NULL);

    if (self->flow_box == NULL) {
        GtkWidget *flow_box;

        self->flow_box = flow_box = gtk_flow_box_new ();
        g_object_set (flow_box,
                      "column-spacing", VIEW_COLUMN_SPACING,
                      "margin", VIEW_MARGIN,
                      "selection-mode", GTK_SELECTION_NONE,
                      "vexpand", TRUE,
                      NULL);
        gtk_flow_box_set_filter_func (GTK_FLOW_BOX (flow_box),
                                      font_view_filter_func,
                                      self, NULL);
        gtk_flow_box_set_sort_func (GTK_FLOW_BOX (flow_box),
                                    font_view_sort_func,
                                    self, NULL);
        g_signal_connect (flow_box, "child-activated",
                          G_CALLBACK (view_child_activated_cb), self);
        gtk_container_add (GTK_CONTAINER (self->swin_view), flow_box);

        /* Instead of using gtk_flow_box_bind_model(), we populate the view
         * manually, since we want to support filtering and sorting through
         * the flowbox, which somehow gtk_flow_box_bind_model() does not support.
         */
        font_view_populate_from_model
            (self, 0, 0,
             g_list_model_get_n_items (font_view_model_get_list_model (self->model)));
    }

    gtk_widget_show_all (self->main_window);
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "overview");
}

static gboolean
font_view_window_key_press_event_cb (GtkWidget *widget,
                                     GdkEventKey *event,
                                     gpointer user_data)
{
    FontViewApplication *self = user_data;

    if (event->keyval == GDK_KEY_q &&
        (event->state & GDK_CONTROL_MASK) != 0) {
        g_application_quit (G_APPLICATION (self));
        return TRUE;
    }

    if (event->keyval == GDK_KEY_f &&
        (event->state & GDK_CONTROL_MASK) != 0) {
        gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (self->search_bar), TRUE);
        return TRUE;
    }

    return gtk_search_bar_handle_event (GTK_SEARCH_BAR (self->search_bar), (GdkEvent *)event);
}

static void
query_info_ready_cb (GObject *object,
                     GAsyncResult *res,
                     gpointer user_data)
{
    FontViewApplication *self = user_data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GFileInfo) info = NULL;

    ensure_window (self);
    g_application_release (G_APPLICATION (self));

    info = g_file_query_info_finish (G_FILE (object), res, &error);
    if (error != NULL) {
        font_view_application_do_overview (self);
        font_view_show_font_error (self, error);
    } else {
        font_view_application_do_open (self, G_FILE (object), 0);
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
                             G_FILE_QUERY_INFO_NONE,
                             G_PRIORITY_DEFAULT, NULL,
                             query_info_ready_cb, self);
}

static void
action_quit (GSimpleAction *action,
             GVariant *parameter,
             gpointer user_data)
{
    FontViewApplication *self = user_data;
    gtk_widget_destroy (self->main_window);
}

static void
action_about (GSimpleAction *action,
              GVariant *parameter,
              gpointer user_data)
{
    FontViewApplication *self = user_data;
    const gchar *authors[] = {
        "Cosimo Cecchi",
        "James Henstridge",
        NULL
    };

    gtk_show_about_dialog (GTK_WINDOW (self->main_window),
                           "version", VERSION,
                           "authors", authors,
                           "program-name", _("Fonts"),
                           "comments", _("View fonts on your system"),
                           "website", "https://gitlab.gnome.org/GNOME/gnome-font-viewer/",
                           "logo-icon-name", FONT_VIEW_ICON_NAME,
                           "translator-credits", _("translator-credits"),
                           "license-type", GTK_LICENSE_GPL_2_0,
                           "wrap-license", TRUE,
                           NULL);

}

static void
action_back (GSimpleAction *action,
             GVariant      *parameter,
             gpointer       user_data)
{
    FontViewApplication *self = user_data;
    font_view_application_do_overview (self);
}

static GActionEntry action_entries[] = {
    { "about", action_about, NULL, NULL, NULL },
    { "back", action_back, NULL, NULL, NULL },
    { "quit", action_quit, NULL, NULL, NULL }
};

static void
search_text_changed (GtkEntry *entry,
                     FontViewApplication *self)
{
    gtk_flow_box_invalidate_filter (GTK_FLOW_BOX (self->flow_box));
}

static void
ensure_window (FontViewApplication *self)
{
    g_autoptr(GtkBuilder) builder = NULL;
    GtkWidget *window, *swin, *box, *image;
    GMenuModel *menu;

    if (self->main_window)
        return;

    self->main_window = window = hdy_application_window_new ();
    gtk_window_set_application (GTK_WINDOW (window), GTK_APPLICATION (self));
    gtk_window_set_resizable (GTK_WINDOW (window), TRUE);
    gtk_window_set_default_size (GTK_WINDOW (window), 800, 600);
    gtk_window_set_icon_name (GTK_WINDOW (window), FONT_VIEW_ICON_NAME);

    self->header = hdy_header_bar_new ();
    hdy_header_bar_set_show_close_button (HDY_HEADER_BAR (self->header), TRUE);
    gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self->header)),
                                 "titlebar");

    g_signal_connect (window, "key-press-event",
                      G_CALLBACK (font_view_window_key_press_event_cb), self);

    self->main_grid = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add (GTK_CONTAINER (self->main_grid), self->header);
    gtk_container_add (GTK_CONTAINER (self->main_window), self->main_grid);

    self->stack = gtk_stack_new ();
    gtk_stack_set_transition_type (GTK_STACK (self->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_container_add (GTK_CONTAINER (self->main_grid), self->stack);
    gtk_widget_set_hexpand (self->stack, TRUE);
    gtk_widget_set_vexpand (self->stack, TRUE);

    box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_stack_add_named (GTK_STACK (self->stack), box, "overview");

    builder = gtk_builder_new ();
    gtk_builder_add_from_resource (builder, "/org/gnome/font-viewer/font-view-app-menu.ui", NULL);
    menu = G_MENU_MODEL (gtk_builder_get_object (builder, "app-menu"));

    self->menu_button = gtk_menu_button_new ();
    image = gtk_image_new_from_icon_name ("open-menu-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_show (image);
    gtk_container_add (GTK_CONTAINER (self->menu_button), image);
    gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (self->menu_button), menu);
    gtk_widget_set_no_show_all (self->menu_button, TRUE);
    gtk_widget_show (self->menu_button);
    hdy_header_bar_pack_end (HDY_HEADER_BAR (self->header), self->menu_button);

    self->search_bar = gtk_search_bar_new ();
    gtk_container_add (GTK_CONTAINER (box), self->search_bar);
    self->search_entry = gtk_search_entry_new ();
    gtk_entry_set_width_chars (GTK_ENTRY (self->search_entry), 40);
    gtk_container_add (GTK_CONTAINER (self->search_bar), self->search_entry);
    self->search_toggle = gtk_toggle_button_new ();
    gtk_widget_set_no_show_all (self->search_toggle, TRUE);
    gtk_widget_show (self->search_toggle);
    image = gtk_image_new_from_icon_name ("edit-find-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_show (image);
    gtk_container_add (GTK_CONTAINER (self->search_toggle), image);
    hdy_header_bar_pack_end (HDY_HEADER_BAR (self->header), self->search_toggle);
    g_object_bind_property (self->search_bar, "search-mode-enabled",
                            self->search_toggle, "active",
                            G_BINDING_BIDIRECTIONAL);

    g_signal_connect (self->search_entry, "search-changed", G_CALLBACK (search_text_changed), self);

    self->swin_view = swin = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swin),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add (GTK_CONTAINER (box), self->swin_view);

    self->swin_preview = swin = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swin),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_stack_add_named (GTK_STACK (self->stack), swin, "preview");

    self->swin_info = swin = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swin),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_stack_add_named (GTK_STACK (self->stack), swin, "info");

    gtk_widget_show_all (window);
}

static void
font_view_application_startup (GApplication *application)
{
    FontViewApplication *self = FONT_VIEW_APPLICATION (application);

    G_APPLICATION_CLASS (font_view_application_parent_class)->startup (application);

    hdy_init ();

    if (!FcInit ())
        g_critical ("Can't initialize fontconfig library");

    g_action_map_add_action_entries (G_ACTION_MAP (self), action_entries,
                                     G_N_ELEMENTS (action_entries), self);

    const gchar *back_accels[] = { "<Alt>Left", NULL };
    gtk_application_set_accels_for_action (GTK_APPLICATION (application),
                                           "app.back",
                                           back_accels);
}

static void
font_view_application_activate (GApplication *application)
{
    FontViewApplication *self = FONT_VIEW_APPLICATION (application);

    ensure_window (self);
    font_view_application_do_overview (self);
}

static void
font_view_application_dispose (GObject *obj)
{
    FontViewApplication *self = FONT_VIEW_APPLICATION (obj);

    g_cancellable_cancel (self->cancellable);

    g_clear_object (&self->cancellable);
    g_clear_object (&self->font_file);
    g_clear_object (&self->model);

    G_OBJECT_CLASS (font_view_application_parent_class)->dispose (obj);
}

static void
font_view_application_init (FontViewApplication *self)
{
    /* do nothing */
}

static void
font_view_application_class_init (FontViewApplicationClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);
    GApplicationClass *aclass = G_APPLICATION_CLASS (klass);

    aclass->activate = font_view_application_activate;
    aclass->startup = font_view_application_startup;
    aclass->open = font_view_application_open;

    oclass->dispose = font_view_application_dispose;
}

static GApplication *
font_view_application_new (void)
{
    return g_object_new (FONT_VIEW_TYPE_APPLICATION,
                         "application-id", APPLICATION_ID,
                         "flags", G_APPLICATION_HANDLES_OPEN,
                         NULL);
}

int
main (int argc,
      char **argv)
{
    g_autoptr(GApplication) app = NULL;
    gint retval;

    bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    app = font_view_application_new ();
    g_application_add_main_option_entries (app, goption_options);
    retval = g_application_run (app, argc, argv);

    return retval;
}
