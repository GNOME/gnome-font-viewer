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

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-desktop-thumbnail.h>

#include "font-model.h"
#include "sushi-font-widget.h"

#define FONT_VIEW_TYPE_APPLICATION (font_view_application_get_type ())
#define FONT_VIEW_ICON_NAME APPLICATION_ID

G_DECLARE_FINAL_TYPE (FontViewApplication,
                      font_view_application,
                      FONT_VIEW,
                      APPLICATION,
                      GtkApplication)

struct _FontViewApplication
{
    GtkApplication parent;

    GtkApplicationWindow *main_window;
    GtkWidget *main_grid;
    GtkWidget *header;
    GtkWidget *title_label;
    GtkWidget *side_grid;
    GtkWidget *font_widget;
    GtkToggleButton *info_button;
    GtkButton *install_button;
    GtkWidget *back_button;
    GtkWidget *stack;
    GtkWidget *swin_view;
    GtkWidget *swin_preview;
    GtkWidget *swin_info;
    GtkWidget *grid_view;
    GtkWidget *search_bar;
    GtkWidget *search_entry;
    GtkWidget *search_toggle;
    GtkWidget *menu_button;

    GtkFilter *filter;
    GtkSorter *sorter;

    FontViewModel *model;

    GFile *font_file;

    GCancellable *cancellable;
};

G_DEFINE_TYPE (FontViewApplication,
               font_view_application,
               GTK_TYPE_APPLICATION);

G_DECLARE_FINAL_TYPE (
    FontViewItem, font_view_item, FONT_VIEW, ITEM, GtkFlowBoxChild);

struct _FontViewItem
{
    GtkFlowBoxChild parent;

    GtkWidget *font_preview;
    GtkWidget *label;

    GCancellable *thumbnail_cancellable;
};

#define FONT_VIEW_TYPE_ITEM (font_view_item_get_type ())

G_DEFINE_TYPE (FontViewItem, font_view_item, GTK_TYPE_BOX)

static void
font_view_item_dispose (GObject *obj)
{
    FontViewItem *self = FONT_VIEW_ITEM (obj);

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
    gtk_widget_add_css_class (GTK_WIDGET (self), "font-item");
    gtk_orientable_set_orientation (GTK_ORIENTABLE (self),
                                    GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing (GTK_BOX (self), 6);

    self->font_preview = gtk_label_new (NULL);
    gtk_widget_add_css_class (self->font_preview, "font-preview");
    gtk_widget_set_margin_start (self->font_preview, 6);
    gtk_widget_set_margin_end (self->font_preview, 6);
    gtk_widget_set_halign (self->font_preview, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand (self->font_preview, TRUE);
    gtk_box_append (GTK_BOX (self), self->font_preview);

    self->label = gtk_label_new (NULL);
    gtk_widget_set_halign (self->label, GTK_ALIGN_CENTER);
    gtk_label_set_wrap (GTK_LABEL (self->label), TRUE);
    gtk_label_set_wrap_mode (GTK_LABEL (self->label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars (GTK_LABEL (self->label), 18);
    gtk_label_set_justify (GTK_LABEL (self->label), GTK_JUSTIFY_CENTER);
    gtk_box_append (GTK_BOX (self), self->label);
}

#define ATTRIBUTES_FOR_CREATING_THUMBNAIL                                      \
    G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE "," G_FILE_ATTRIBUTE_TIME_MODIFIED
#define ATTRIBUTES_FOR_EXISTING_THUMBNAIL                                      \
    G_FILE_ATTRIBUTE_THUMBNAIL_PATH "," G_FILE_ATTRIBUTE_THUMBNAILING_FAILED

static GtkWidget *
font_view_item_new ()
{
    FontViewItem *view_item = g_object_new (FONT_VIEW_TYPE_ITEM, NULL);

    return GTK_WIDGET (view_item);
}

static void
font_view_item_bind (FontViewItem *self, FontViewModelItem *item)
{
    gtk_label_set_text (GTK_LABEL (self->label),
                        font_view_model_item_get_font_name (item));
    gtk_label_set_text (GTK_LABEL (self->font_preview),
                        font_view_model_item_get_font_preview_text (item));

    PangoAttrList *list = pango_attr_list_new ();
    PangoAttribute *attr = pango_attr_font_desc_new (
        font_view_model_item_get_font_description (item));
    pango_attr_list_insert (list, attr);
    gtk_label_set_attributes (GTK_LABEL (self->font_preview), list);
}

static void
font_view_item_unbind (FontViewItem *self)
{
    gtk_label_set_text (GTK_LABEL (self->label), NULL);
    gtk_label_set_text (GTK_LABEL (self->font_preview), NULL);
    gtk_label_set_attributes (GTK_LABEL (self->font_preview), NULL);

    g_cancellable_cancel (self->thumbnail_cancellable);
    g_clear_object (&self->thumbnail_cancellable);
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
    g_print ("%s %s\n", _ ("GNOME Fonts"), VERSION);
    exit (EXIT_SUCCESS);
    return TRUE;
}

static const GOptionEntry goption_options[] = {
    {"version", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
     _print_version_and_exit, N_ ("Show the application's version"), NULL},
    {NULL}};

#define WHITESPACE_CHARS "\f \t"

static void
strip_whitespace (gchar **original)
{
    g_auto (GStrv) split = NULL;
    g_autoptr (GString) reassembled = NULL;
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
add_row (GtkBox *list,
         const gchar *name,
         const gchar *value,
         gboolean multiline)
{
    GtkWidget *name_w, *label;
    int i;
    const char *p;
    GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    name_w = gtk_label_new (name);
    gtk_widget_add_css_class (GTK_WIDGET (name_w), "dim-label");
    gtk_widget_set_halign (name_w, GTK_ALIGN_END);
    gtk_widget_set_valign (name_w, GTK_ALIGN_START);

    gtk_box_append (GTK_BOX (hbox), name_w);

    label = gtk_label_new (value);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_widget_set_valign (label, GTK_ALIGN_START);
    gtk_label_set_selectable (GTK_LABEL (label), TRUE);

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

    gtk_box_append (GTK_BOX (hbox), label);
    gtk_box_append (list, hbox);
}

#define FixedToFloat(f) (((float) (f)) / 65536.0)

static char *
describe_axis (FT_Var_Axis *ax)
{
    /* Translators, this string is used to display information about
     * a 'font variation axis'. The %s gets replaced with the name
     * of the axis, for example 'Width'. The three %g get replaced
     * with the minimum, maximum and default values for the axis.
     */
    return g_strdup_printf (_ ("%s %g — %g, default %g"), ax->name,
                            FixedToFloat (ax->minimum),
                            FixedToFloat (ax->maximum), FixedToFloat (ax->def));
}

static char *
get_sfnt_name (FT_Face face, guint id)
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

        return g_convert ((gchar *) sname.string, sname.string_len, "UTF-8",
                          "UTF-16BE", NULL, NULL, NULL);
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
describe_instance (FT_Face face, FT_Var_Named_Style *ns, int pos, GString *s)
{
    g_autofree char *str = NULL;

    if (is_valid_subfamily_id (ns->strid))
        str = get_sfnt_name (face, ns->strid);

    if (str == NULL)
        str = g_strdup_printf (_ ("Instance %d"), pos);

    if (s->len > 0)
        g_string_append (s, ", ");
    g_string_append (s, str);
}

#include "open-type-layout.h"

static char *
get_features (FT_Face face)
{
    g_autoptr (GString) s = NULL;
    hb_font_t *hb_font;
    int i, j, k;

    s = g_string_new ("");

    hb_font = hb_ft_font_create (face, NULL);
    if (hb_font) {
        hb_tag_t tables[2] = {HB_OT_TAG_GSUB, HB_OT_TAG_GPOS};
        hb_face_t *hb_face;

        hb_face = hb_font_get_face (hb_font);

        for (i = 0; i < 2; i++) {
            hb_tag_t features[80];
            unsigned int count = G_N_ELEMENTS (features);
            unsigned int script_index = 0;
            unsigned int lang_index = HB_OT_LAYOUT_DEFAULT_LANGUAGE_INDEX;

            hb_ot_layout_language_get_feature_tags (hb_face, tables[i],
                                                    script_index, lang_index, 0,
                                                    &count, features);
            for (j = 0; j < count; j++) {
                for (k = 0; k < G_N_ELEMENTS (open_type_layout_features); k++) {
                    if (open_type_layout_features[k].tag == features[j]) {
                        if (s->len > 0)
                            /* Translators, this seperates the list of Layout
                             * Features. */
                            g_string_append (s, C_ ("OpenType layout", ", "));
                        g_string_append (
                            s,
                            g_dpgettext2 (NULL, "OpenType layout",
                                          open_type_layout_features[k].name));
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
populate_grid (FontViewApplication *self, GtkBox *grid, FT_Face face)
{
    g_autoptr (GFileInfo) info = NULL;
    g_autofree gchar *path = NULL;
    PS_FontInfoRec ps_info;

    add_row (grid, _ ("Name"), face->family_name, FALSE);

    path = g_file_get_path (self->font_file);
    add_row (grid, _ ("Location"), path, FALSE);

    if (face->style_name)
        add_row (grid, _ ("Style"), face->style_name, FALSE);

    info = g_file_query_info (self->font_file,
                              G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE
                              "," G_FILE_ATTRIBUTE_STANDARD_SIZE,
                              G_FILE_QUERY_INFO_NONE, NULL, NULL);

    if (info != NULL) {
        g_autofree gchar *s = g_content_type_get_description (
            g_file_info_get_content_type (info));
        add_row (grid, _ ("Type"), s, FALSE);
    }

    if (FT_IS_SFNT (face)) {
        gint i, len;
        g_autofree gchar *version = NULL, *copyright = NULL,
                         *description = NULL;
        g_autofree gchar *designer = NULL, *manufacturer = NULL,
                         *license = NULL;

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
                    copyright =
                        g_convert ((gchar *) sname.string, sname.string_len,
                                   "UTF-8", "UTF-16BE", NULL, NULL, NULL);
                break;
            case TT_NAME_ID_VERSION_STRING:
                if (!version)
                    version =
                        g_convert ((gchar *) sname.string, sname.string_len,
                                   "UTF-8", "UTF-16BE", NULL, NULL, NULL);
                break;
            case TT_NAME_ID_DESCRIPTION:
                if (!description)
                    description =
                        g_convert ((gchar *) sname.string, sname.string_len,
                                   "UTF-8", "UTF-16BE", NULL, NULL, NULL);
                break;
            case TT_NAME_ID_MANUFACTURER:
                if (!manufacturer)
                    manufacturer =
                        g_convert ((gchar *) sname.string, sname.string_len,
                                   "UTF-8", "UTF-16BE", NULL, NULL, NULL);
                break;
            case TT_NAME_ID_DESIGNER:
                if (!designer)
                    designer =
                        g_convert ((gchar *) sname.string, sname.string_len,
                                   "UTF-8", "UTF-16BE", NULL, NULL, NULL);
                break;
            case TT_NAME_ID_LICENSE:
                if (!license)
                    license =
                        g_convert ((gchar *) sname.string, sname.string_len,
                                   "UTF-8", "UTF-16BE", NULL, NULL, NULL);
                break;
            default:
                break;
            }
        }
        if (version) {
            strip_version (&version);
            add_row (grid, _ ("Version"), version, FALSE);
        }
        if (copyright) {
            strip_whitespace (&copyright);
            add_row (grid, _ ("Copyright"), copyright, TRUE);
        }
        if (description) {
            strip_whitespace (&description);
            add_row (grid, _ ("Description"), description, TRUE);
        }
        if (manufacturer) {
            strip_whitespace (&manufacturer);
            add_row (grid, _ ("Manufacturer"), manufacturer, TRUE);
        }
        if (designer) {
            strip_whitespace (&designer);
            add_row (grid, _ ("Designer"), designer, TRUE);
        }
        if (license) {
            strip_whitespace (&license);
            add_row (grid, _ ("License"), license, TRUE);
        }
    } else if (FT_Get_PS_Font_Info (face, &ps_info) == 0) {
        if (ps_info.version && g_utf8_validate (ps_info.version, -1, NULL)) {
            g_autofree gchar *compressed = g_strcompress (ps_info.version);
            strip_version (&compressed);
            add_row (grid, _ ("Version"), compressed, FALSE);
        }
        if (ps_info.notice && g_utf8_validate (ps_info.notice, -1, NULL)) {
            g_autofree gchar *compressed = g_strcompress (ps_info.notice);
            strip_whitespace (&compressed);
            add_row (grid, _ ("Copyright"), compressed, TRUE);
        }
    }
}

static void
populate_details (FontViewApplication *self, GtkBox *grid, FT_Face face)
{
    g_autofree gchar *glyph_count = NULL, *features = NULL;
    FT_MM_Var *ft_mm_var;

    glyph_count = g_strdup_printf ("%ld", face->num_glyphs);
    add_row (grid, _ ("Glyph Count"), glyph_count, FALSE);

    add_row (grid, _ ("Color Glyphs"),
             FT_HAS_COLOR (face) ? _ ("yes") : _ ("no"), FALSE);

    features = get_features (face);
    if (features)
        add_row (grid, _ ("Layout Features"), features, TRUE);

    if (FT_Get_MM_Var (face, &ft_mm_var) == 0) {
        int i;
        for (i = 0; i < ft_mm_var->num_axis; i++) {
            g_autofree gchar *s = describe_axis (&ft_mm_var->axis[i]);
            add_row (grid, i == 0 ? _ ("Variation Axes") : "", s, FALSE);
        }
        {
            g_autoptr (GString) str = g_string_new ("");
            for (i = 0; i < ft_mm_var->num_namedstyles; i++)
                describe_instance (face, &ft_mm_var->namedstyle[i], i, str);

            add_row (grid, _ ("Named Styles"), str->str, TRUE);
        }
        free (ft_mm_var);
    }
}

static void
install_button_refresh_appearance (FontViewApplication *self, GError *error)
{
    FT_Face face;

    if (error != NULL) {
        gtk_button_set_label (self->install_button, _ ("Failed"));
        gtk_widget_set_sensitive (GTK_WIDGET (self->install_button), FALSE);
        gtk_widget_remove_css_class (GTK_WIDGET (self->install_button),
                                     "suggested-action");
    } else {
        face = sushi_font_widget_get_ft_face (
            SUSHI_FONT_WIDGET (self->font_widget));

        if (font_view_model_has_face (FONT_VIEW_MODEL (self->model), face)) {
            gtk_button_set_label (self->install_button, _ ("Installed"));
            gtk_widget_set_sensitive (GTK_WIDGET (self->install_button), FALSE);
            gtk_widget_remove_css_class (GTK_WIDGET (self->install_button),
                                         "suggested-action");
        } else if (self->cancellable != NULL) {
            gtk_button_set_label (self->install_button, _ ("Installing"));
            gtk_widget_set_sensitive (GTK_WIDGET (self->install_button), FALSE);
        } else {
            gtk_button_set_label (self->install_button, _ ("Install"));
            gtk_widget_set_sensitive (GTK_WIDGET (self->install_button), TRUE);
            gtk_widget_add_css_class (GTK_WIDGET (self->install_button),
                                      "suggested-action");
        }
    }
}

static char *
font_name_closure (gpointer self)
{
    FontViewModelItem *item = FONT_VIEW_MODEL_ITEM (self);

    return g_strdup (font_view_model_item_get_font_name (item));
}

static void
font_item_setup (GtkSignalListItemFactory *self,
                 GtkListItem *listitem,
                 gpointer user_data)
{
    gtk_list_item_set_activatable (listitem, true);
    gtk_list_item_set_child (listitem, font_view_item_new ());
}

static void
font_item_bind (GtkSignalListItemFactory *self,
                GtkListItem *listitem,
                gpointer user_data)
{
    FontViewModelItem *model_item = gtk_list_item_get_item (listitem);
    FontViewItem *item = FONT_VIEW_ITEM (gtk_list_item_get_child (listitem));
    font_view_item_bind (item, model_item);
}

static void
font_item_unbind (GtkSignalListItemFactory *self,
                  GtkListItem *listitem,
                  gpointer user_data)
{
    FontViewItem *item = FONT_VIEW_ITEM (gtk_list_item_get_child (listitem));
    font_view_item_unbind (item);
}

static void
font_item_teardown (GtkSignalListItemFactory *self,
                    GtkListItem *listitem,
                    gpointer user_data)
{
}

static void
font_view_create_grid_view (FontViewApplication *self)
{

    GListModel *list_model = font_view_model_get_list_model (self->model);
    GtkFilter *filter =
        GTK_FILTER (gtk_string_filter_new (gtk_closure_expression_new (
            G_TYPE_STRING,
            g_cclosure_new (G_CALLBACK (font_name_closure), NULL, NULL), 0,
            NULL)));
    self->filter = filter;
    GtkSorter *sorter =
        GTK_SORTER (gtk_string_sorter_new (gtk_closure_expression_new (
            G_TYPE_STRING,
            g_cclosure_new (G_CALLBACK (font_name_closure), NULL, NULL), 0,
            NULL)));
    self->sorter = sorter;

    GtkSortListModel *sort_model = gtk_sort_list_model_new (list_model, sorter);
    GtkFilterListModel *filter_model =
        gtk_filter_list_model_new (G_LIST_MODEL (sort_model), filter);
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();

    g_signal_connect (factory, "setup", G_CALLBACK (font_item_setup), NULL);
    g_signal_connect (factory, "bind", G_CALLBACK (font_item_bind), NULL);
    g_signal_connect (factory, "unbind", G_CALLBACK (font_item_unbind), NULL);
    g_signal_connect (factory, "teardown", G_CALLBACK (font_item_teardown),
                      NULL);

    GtkNoSelection *selection =
        gtk_no_selection_new (G_LIST_MODEL (filter_model));
    self->grid_view =
        gtk_grid_view_new (GTK_SELECTION_MODEL (selection), factory);
    gtk_grid_view_set_single_click_activate (GTK_GRID_VIEW (self->grid_view),
                                             true);
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
}

static void
font_view_show_error (FontViewApplication *self,
                      const gchar *primary_text,
                      const gchar *secondary_text)
{
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new (GTK_WINDOW (self->main_window),
                                     GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                     GTK_BUTTONS_CLOSE, "%s", primary_text);
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s",
                                              secondary_text);
    g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_unparent),
                      NULL);
    gtk_widget_show (dialog);
}

static void
font_view_show_install_error (FontViewApplication *self, GError *error)
{
    install_button_refresh_appearance (self, error);
    font_view_show_error (self, _ ("This font could not be installed."),
                          error->message);
}

static void
font_install_finished (GObject *source_object,
                       GAsyncResult *res,
                       gpointer user_data)
{
    FontViewApplication *self = user_data;
    g_autoptr (GError) err = NULL;

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
    g_autoptr (GError) error = NULL;
    gboolean created = FALSE;
    gint i = 0;

    while (!created) {
        g_autofree gchar *dest_filename =
            (i == 0) ? g_strdup (dest_basename)
                     : g_strdup_printf ("%d%s", i, dest_basename);
        g_autoptr (GFile) dest_file =
            g_file_get_child (dest_location, dest_filename);

        created = g_file_copy (self->font_file, dest_file, G_FILE_COPY_NONE,
                               cancellable, NULL, NULL, &error);

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
font_view_install_font (FontViewApplication *self, GFile *dest_location)
{
    g_autoptr (GTask) task = NULL;

    self->cancellable = g_cancellable_new ();

    task = g_task_new (self, self->cancellable, font_install_finished, self);
    g_task_set_task_data (task, g_object_ref (dest_location), g_object_unref);
    g_task_run_in_thread (task, install_font_job);
}

static void
install_button_clicked_cb (GtkButton *button, gpointer user_data)
{
    FontViewApplication *self = user_data;
    g_autoptr (GError) err = NULL;
    g_autoptr (GFile) home_prefix = NULL, xdg_prefix = NULL;
    g_autoptr (GFile) xdg_location = NULL, home_location = NULL,
                      dest_location = NULL;
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
        g_autoptr (GFile) file = g_file_new_for_path ((const gchar *) path);

        if (g_file_has_prefix (file, xdg_prefix)) {
            xdg_location = g_steal_pointer (&file);
            break;
        }

        if ((home_location == NULL) && g_file_has_prefix (file, home_prefix)) {
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
        g_warning (
            "Install failed: can't find any configured user font directory.");
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
font_view_show_font_error (FontViewApplication *self, GError *error)
{
    font_view_show_error (self, _ ("This font could not be displayed."),
                          error->message);
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
font_widget_loaded_cb (SushiFontWidget *font_widget, gpointer user_data)
{
    FontViewApplication *self = user_data;
    FT_Face face = sushi_font_widget_get_ft_face (font_widget);
    GtkWidget *title = adw_window_title_new (NULL, NULL);
    const gchar *uri;

    if (face == NULL)
        return;

    uri = sushi_font_widget_get_uri (font_widget);
    self->font_file = g_file_new_for_uri (uri);

    if (face->family_name) {
        adw_window_title_set_title (ADW_WINDOW_TITLE (title), face->family_name);
    } else {
        g_autofree gchar *basename = g_file_get_basename (self->font_file);
        adw_window_title_set_title (ADW_WINDOW_TITLE (title), basename);
    }

    adw_window_title_set_subtitle (ADW_WINDOW_TITLE (title), face->style_name);
    gtk_header_bar_set_title_widget (GTK_HEADER_BAR (self->header), title);

    install_button_refresh_appearance (self, NULL);
}

static void
info_button_clicked_cb (GtkButton *button, gpointer user_data)
{
    FontViewApplication *self = user_data;
    GtkWidget *grid;
    // TODO:
    // GtkWidget *child;
    FT_Face face =
        sushi_font_widget_get_ft_face (SUSHI_FONT_WIDGET (self->font_widget));

    if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
        gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "preview");
        return;
    }

    if (face == NULL)
        return;

    // TODO:
    // child = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW
    // (self->swin_info)); if (child)
    //    gtk_widget_unparent (child);

    grid = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
    gtk_orientable_set_orientation (GTK_ORIENTABLE (grid),
                                    GTK_ORIENTATION_VERTICAL);
    g_object_set (grid, "margin-start", 20, NULL);
    g_object_set (grid, "margin-end", 20, NULL);
    g_object_set (grid, "margin-top", 20, NULL);
    g_object_set (grid, "margin-bottom", 20, NULL);

    populate_grid (self, GTK_BOX (grid), face);
    populate_details (self, GTK_BOX (grid), face);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->swin_info), grid);

    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "info");
}

static void
font_view_ensure_model (FontViewApplication *self)
{
    if (self->model != NULL)
        return;

    self->model = font_view_model_new ();
    g_signal_connect (font_view_model_get_list_model (self->model),
                      "items-changed", G_CALLBACK (font_model_items_changed_cb),
                      self);
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
        g_autoptr (GtkSizeGroup) install_size_group =
            gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

        self->install_button = GTK_BUTTON (gtk_button_new ());
        gtk_button_set_label (self->install_button, _ ("Install"));
        gtk_widget_set_valign (GTK_WIDGET (self->install_button),
                               GTK_ALIGN_CENTER);
        gtk_widget_add_css_class (GTK_WIDGET (self->install_button),
                                  "text-button");
        gtk_header_bar_pack_end (GTK_HEADER_BAR (self->header),
                                 GTK_WIDGET (self->install_button));

        g_signal_connect (self->install_button, "clicked",
                          G_CALLBACK (install_button_clicked_cb), self);
    }

    if (self->info_button == NULL) {
        self->info_button =
            GTK_TOGGLE_BUTTON (gtk_toggle_button_new_with_label (_ ("Info")));
        gtk_widget_set_valign (GTK_WIDGET (self->info_button),
                               GTK_ALIGN_CENTER);
        gtk_widget_add_css_class (GTK_WIDGET (self->info_button),
                                  "text-button");
        gtk_header_bar_pack_end (GTK_HEADER_BAR (self->header),
                                 GTK_WIDGET (self->info_button));

        g_signal_connect (self->info_button, "toggled",
                          G_CALLBACK (info_button_clicked_cb), self);
    }

    if (self->back_button == NULL) {
        self->back_button = gtk_button_new ();
        gtk_button_set_icon_name (GTK_BUTTON (self->back_button),
                                  "go-previous-symbolic");
        gtk_widget_set_tooltip_text (self->back_button, _ ("Back"));
        gtk_widget_set_valign (self->back_button, GTK_ALIGN_CENTER);
        gtk_widget_add_css_class (self->back_button, "image-button");
        gtk_header_bar_pack_start (GTK_HEADER_BAR (self->header),
                                   self->back_button);

        gtk_actionable_set_action_name (GTK_ACTIONABLE (self->back_button),
                                        "app.back");
    }

    gtk_widget_hide (self->search_toggle);
    gtk_widget_hide (self->menu_button);

    uri = g_file_get_uri (file);

    if (self->font_widget == NULL) {
        GtkWidget *viewport;

        self->font_widget =
            GTK_WIDGET (sushi_font_widget_new (uri, face_index));
        gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->swin_preview),
                                       self->font_widget);
        viewport = gtk_widget_get_parent (self->font_widget);
        gtk_scrollable_set_hscroll_policy (GTK_SCROLLABLE (viewport),
                                           GTK_SCROLL_NATURAL);
        gtk_scrollable_set_vscroll_policy (GTK_SCROLLABLE (viewport),
                                           GTK_SCROLL_NATURAL);

        g_signal_connect (self->font_widget, "loaded",
                          G_CALLBACK (font_widget_loaded_cb), self);
        g_signal_connect (self->font_widget, "error",
                          G_CALLBACK (font_widget_error_cb), self);
    } else {
        g_object_set (self->font_widget, "uri", uri, "face-index", face_index,
                      NULL);
        sushi_font_widget_load (SUSHI_FONT_WIDGET (self->font_widget));
    }

    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "preview");
    gtk_toggle_button_set_active (self->info_button, FALSE);
}

static void
view_child_activated_cb (GtkGridView *grid_view,
                         guint position,
                         gpointer user_data)
{
    FontViewApplication *self = user_data;
    GtkSelectionModel *model = gtk_grid_view_get_model (grid_view);
    FontViewModelItem *item = FONT_VIEW_MODEL_ITEM (
        g_list_model_get_item (G_LIST_MODEL (model), position));
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

    g_clear_pointer (&self->back_button, gtk_widget_unparent);

    if (self->info_button) {
        gtk_widget_unparent (GTK_WIDGET (self->info_button));
        self->info_button = NULL;
    }

    if (self->install_button) {
        gtk_widget_unparent (GTK_WIDGET (self->install_button));
        self->install_button = NULL;
    }

    gtk_widget_show (self->search_toggle);
    gtk_widget_show (self->menu_button);

    font_view_ensure_model (self);
    GtkWidget *title = adw_window_title_new (_ ("All Fonts"), NULL);
    gtk_header_bar_set_title_widget (GTK_HEADER_BAR (self->header), title);

    if (self->grid_view == NULL) {
        GtkWidget *grid_view;

        font_view_create_grid_view (self);
        grid_view = self->grid_view;

        g_object_set (grid_view, "vexpand", TRUE, NULL);

        // TODO: Activate might not be correct/setup correctly
        g_signal_connect (grid_view, "activate",
                          G_CALLBACK (view_child_activated_cb), self);
        gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->swin_view),
                                       grid_view);
    }

    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "overview");
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
                             G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT, NULL,
                             query_info_ready_cb, self);
}

static void
action_quit (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    FontViewApplication *self = user_data;
    gtk_window_destroy (GTK_WINDOW (self->main_window));
}

static void
action_about (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    FontViewApplication *self = user_data;

    // clang-format off
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
    // clang-format on
}

static void
action_back (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    FontViewApplication *self = user_data;
    font_view_application_do_overview (self);
}

static GActionEntry action_entries[] = {
    {"about", action_about, NULL, NULL, NULL},
    {"back", action_back, NULL, NULL, NULL},
    {"quit", action_quit, NULL, NULL, NULL}};

static void
search_text_changed (GtkEntry *entry, FontViewApplication *self)
{
    const char *search =
        gtk_editable_get_text (GTK_EDITABLE (self->search_entry));

    if (search == NULL || g_strcmp0 (search, "") == 0) {
        gtk_string_filter_set_search (GTK_STRING_FILTER (self->filter), NULL);
        return;
    }

    gtk_string_filter_set_search (GTK_STRING_FILTER (self->filter),
                                  g_strdup (search));
}

static void
ensure_window (FontViewApplication *self)
{
    g_autoptr (GtkBuilder) builder = NULL;
    GtkWidget *swin, *box, *image;
    GtkApplicationWindow *window;
    GMenuModel *menu;

    if (self->main_window)
        return;

    self->main_window = window = GTK_APPLICATION_WINDOW (
        gtk_application_window_new (GTK_APPLICATION (self)));
    gtk_window_set_resizable (GTK_WINDOW (window), TRUE);
    gtk_window_set_default_size (GTK_WINDOW (window), 800, 600);
    gtk_window_set_icon_name (GTK_WINDOW (window), FONT_VIEW_ICON_NAME);

    self->header = gtk_header_bar_new ();
    gtk_widget_add_css_class (GTK_WIDGET (self->header), "titlebar");

    self->main_grid = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child (GTK_WINDOW (self->main_window), self->main_grid);

    self->stack = gtk_stack_new ();
    gtk_stack_set_transition_type (GTK_STACK (self->stack),
                                   GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_box_append (GTK_BOX (self->main_grid), self->stack);
    gtk_widget_set_hexpand (self->stack, TRUE);
    gtk_widget_set_vexpand (self->stack, TRUE);

    box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_stack_add_named (GTK_STACK (self->stack), box, "overview");

    builder = gtk_builder_new ();
    gtk_builder_add_from_resource (
        builder, "/org/gnome/font-viewer/font-view-app-menu.ui", NULL);
    menu = G_MENU_MODEL (gtk_builder_get_object (builder, "app-menu"));

    self->menu_button = gtk_menu_button_new ();
    gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (self->menu_button),
                                   "open-menu-symbolic");
    gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (self->menu_button), menu);

    gtk_header_bar_pack_end (GTK_HEADER_BAR (self->header), self->menu_button);

    self->search_bar = gtk_search_bar_new ();
    gtk_box_append (GTK_BOX (box), self->search_bar);
    gtk_search_bar_set_key_capture_widget (GTK_SEARCH_BAR (self->search_bar),
                                           GTK_WIDGET (window));
    self->search_entry = gtk_search_entry_new ();
    gtk_search_bar_set_child (GTK_SEARCH_BAR (self->search_bar),
                              self->search_entry);
    self->search_toggle = gtk_toggle_button_new ();

    gtk_button_set_icon_name (GTK_BUTTON (self->search_toggle),
                              "edit-find-symbolic");
    gtk_header_bar_pack_end (GTK_HEADER_BAR (self->header),
                             self->search_toggle);
    g_object_bind_property (self->search_bar, "search-mode-enabled",
                            self->search_toggle, "active",
                            G_BINDING_BIDIRECTIONAL);

    g_signal_connect (self->search_entry, "search-changed",
                      G_CALLBACK (search_text_changed), self);

    self->swin_view = swin = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swin),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_append (GTK_BOX (box), self->swin_view);

    self->swin_preview = swin = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swin),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_stack_add_named (GTK_STACK (self->stack), swin, "preview");

    self->swin_info = swin = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swin),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_stack_add_named (GTK_STACK (self->stack), swin, "info");

    gtk_window_set_titlebar (GTK_WINDOW (window),
                             GTK_HEADER_BAR (self->header));
    gtk_window_present (GTK_WINDOW (window));
}

static void
font_view_application_startup (GApplication *application)
{
    FontViewApplication *self = FONT_VIEW_APPLICATION (application);

    G_APPLICATION_CLASS (font_view_application_parent_class)
        ->startup (application);

    adw_init ();

    if (!FcInit ())
        g_critical ("Can't initialize fontconfig library");

    g_action_map_add_action_entries (G_ACTION_MAP (self), action_entries,
                                     G_N_ELEMENTS (action_entries), self);

    const gchar *back_accels[] = {"<Alt>Left", NULL};
    gtk_application_set_accels_for_action (GTK_APPLICATION (application),
                                           "app.back", back_accels);

    GtkCssProvider *provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_resource (
        GTK_CSS_PROVIDER (provider), "/org/gnome/font-viewer/application.css");

    gtk_style_context_add_provider_for_display (
        gdk_display_get_default (), GTK_STYLE_PROVIDER (provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
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
    return g_object_new (FONT_VIEW_TYPE_APPLICATION, "application-id",
                         APPLICATION_ID, "flags", G_APPLICATION_HANDLES_OPEN,
                         NULL);
}

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
