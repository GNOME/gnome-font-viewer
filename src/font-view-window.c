/*
 * font-view-window.c
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

#include "config.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TYPE1_TABLES_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H
#include FT_MULTIPLE_MASTERS_H
#include <fontconfig/fontconfig.h>
#include <hb-ft.h>
#include <hb-ot.h>
#include <hb.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <freetype2/ft2build.h>

#include "font-view-window.h"
#include "font-inscription.h"
#include "open-type-layout.h"
#include "sushi-font-widget.h"
#include "font-model.h"

#define WHITESPACE_CHARS "\f \t"
#define MATCH_VERSION_STR "Version"
#define FixedToFloat(f) (((float) (f)) / 65536.0)

struct _FontViewWindow
{
  AdwApplicationWindow parent_instance;

  AdwNavigationView *nav_view;

  /* Overview */
  GtkSearchEntry *search_entry;
  GtkGridView *grid_view;

  /* Preview */
  AdwWindowTitle *font_title;
  GtkButton *install_button;
  GtkToggleButton *info_button;
  GtkStack *preview_stack;
  GtkScrolledWindow *swin_preview;
  GtkViewport *viewport_preview;
  AdwPreferencesPage *swin_info;
  SushiFontWidget *font_widget;

  AdwAlertDialog *error_dialog;

  FontViewModel *model;
  GtkSortListModel *sort_model;

  GFile *font_file;

  GCancellable *cancellable;
};

G_DEFINE_FINAL_TYPE (FontViewWindow, font_view_window, ADW_TYPE_APPLICATION_WINDOW)

static char *
font_name_closure (FontViewModelItem *item)
{
  return g_strdup (font_view_model_item_get_font_name (item));
}

static void
install_button_refresh_appearance (FontViewWindow *self,
                                   GError         *error)
{
  FT_Face face;

  if (error != NULL) {
    gtk_button_set_label (self->install_button, _ ("Failed"));
    gtk_widget_set_sensitive (GTK_WIDGET (self->install_button), FALSE);
    gtk_widget_remove_css_class (GTK_WIDGET (self->install_button),
                                 "suggested-action");
  } else {
    face = sushi_font_widget_get_ft_face (SUSHI_FONT_WIDGET (self->font_widget));

    if (font_view_model_has_face (FONT_VIEW_MODEL (self->model), face)) {
        gtk_button_set_label (self->install_button, _ ("Installed"));
        gtk_widget_set_sensitive (GTK_WIDGET (self->install_button), FALSE);
        gtk_widget_remove_css_class (GTK_WIDGET (self->install_button),
                                     "suggested-action");
    } else if (self->cancellable != NULL) {
        gtk_button_set_label (self->install_button, _ ("Installing"));
        gtk_widget_set_sensitive (GTK_WIDGET (self->install_button), FALSE);
    } else {
        gtk_button_set_label (self->install_button, _ ("_Install"));
        gtk_widget_set_sensitive (GTK_WIDGET (self->install_button), TRUE);
        gtk_widget_add_css_class (GTK_WIDGET (self->install_button),
                                  "suggested-action");
    }
  }
}

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
add_row (AdwPreferencesGroup      *group,
         const gchar              *name,
         const gchar              *value,
         gboolean                 multiline)
{
    GtkWidget *row = adw_action_row_new ();

    adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (row), FALSE);
    adw_preferences_row_set_use_underline (ADW_PREFERENCES_ROW (row), FALSE);
    adw_action_row_set_subtitle_selectable (ADW_ACTION_ROW (row), TRUE);
    gtk_widget_add_css_class (row, "property");

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), name);
    adw_action_row_set_subtitle (ADW_ACTION_ROW (row), value);

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
}

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
get_sfnt_name (FT_Face face,
               guint   id)
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
describe_instance (FT_Face             face,
                   FT_Var_Named_Style *ns,
                   int                 pos,
                   GString            *s)
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

        hb_font_destroy (hb_font);
    }

    if (s->len > 0)
        return g_strdup (s->str);

    return NULL;
}

static void
populate_grid (FontViewWindow              *self,
               AdwPreferencesGroup         *group,
               FT_Face                      face)
{
    g_autoptr (GFileInfo) info = NULL;
    g_autofree gchar *path = NULL;
    PS_FontInfoRec ps_info;

    add_row (group, _ ("Name"), face->family_name, FALSE);

    path = g_file_get_path (self->font_file);
    add_row (group, _ ("Location"), path, FALSE);

    if (face->style_name)
        add_row (group, _ ("Style"), face->style_name, FALSE);

    info = g_file_query_info (self->font_file,
                              G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE
                              "," G_FILE_ATTRIBUTE_STANDARD_SIZE,
                              G_FILE_QUERY_INFO_NONE, NULL, NULL);

    if (info != NULL) {
        g_autofree gchar *s = g_content_type_get_description (
            g_file_info_get_content_type (info));
        add_row (group, _ ("Type"), s, FALSE);
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
            add_row (group, _ ("Version"), version, FALSE);
        }
        if (copyright) {
            strip_whitespace (&copyright);
            add_row (group, _ ("Copyright"), copyright, TRUE);
        }
        if (description) {
            strip_whitespace (&description);
            add_row (group, _ ("Description"), description, TRUE);
        }
        if (manufacturer) {
            strip_whitespace (&manufacturer);
            add_row (group, _ ("Manufacturer"), manufacturer, TRUE);
        }
        if (designer) {
            strip_whitespace (&designer);
            add_row (group, _ ("Designer"), designer, TRUE);
        }
        if (license) {
            strip_whitespace (&license);
            add_row (group, _ ("License"), license, TRUE);
        }
    } else if (FT_Get_PS_Font_Info (face, &ps_info) == 0) {
        if (ps_info.version && g_utf8_validate (ps_info.version, -1, NULL)) {
            g_autofree gchar *compressed = g_strcompress (ps_info.version);
            strip_version (&compressed);
            add_row (group, _ ("Version"), compressed, FALSE);
        }
        if (ps_info.notice && g_utf8_validate (ps_info.notice, -1, NULL)) {
            g_autofree gchar *compressed = g_strcompress (ps_info.notice);
            strip_whitespace (&compressed);
            add_row (group, _ ("Copyright"), compressed, TRUE);
        }
    }
}

static void
populate_details (FontViewWindow              *self,
                  AdwPreferencesGroup         *group,
                  FT_Face                      face)
{
    g_autofree gchar *glyph_count = NULL, *features = NULL;
    FT_MM_Var *ft_mm_var;

    glyph_count = g_strdup_printf ("%ld", face->num_glyphs);
    add_row (group, _ ("Glyph Count"), glyph_count, FALSE);

    add_row (group, _ ("Color Glyphs"),
             FT_HAS_COLOR (face) ? _ ("yes") : _ ("no"), FALSE);

    features = get_features (face);
    if (features)
        add_row (group, _ ("Layout Features"), features, TRUE);

    if (FT_Get_MM_Var (face, &ft_mm_var) == 0) {
        int i;
        for (i = 0; i < ft_mm_var->num_axis; i++) {
            g_autofree gchar *s = describe_axis (&ft_mm_var->axis[i]);
            add_row (group, i == 0 ? _ ("Variation Axes") : "", s, FALSE);
        }
        {
            g_autoptr (GString) str = g_string_new ("");
            for (i = 0; i < ft_mm_var->num_namedstyles; i++)
                describe_instance (face, &ft_mm_var->namedstyle[i], i, str);

            add_row (group, _ ("Named Styles"), str->str, TRUE);
        }
        free (ft_mm_var);
    }
}

static void
load_font_info (FontViewWindow *self)
{
  GtkWidget *new_group;
  AdwPreferencesGroup *old_group;
  FT_Face face =
        sushi_font_widget_get_ft_face (SUSHI_FONT_WIDGET (self->font_widget));

  if (face == NULL)
    return;

  old_group = adw_preferences_page_get_group (self->swin_info, 0);

  if (old_group != NULL) {
    adw_preferences_page_remove (self->swin_info, old_group);
  }

  new_group = adw_preferences_group_new ();

  populate_grid (self, ADW_PREFERENCES_GROUP (new_group), face);
  populate_details (self, ADW_PREFERENCES_GROUP (new_group), face);

  adw_preferences_page_add (self->swin_info, ADW_PREFERENCES_GROUP (new_group));
}

static void
font_install_finished (GObject *source_object,
                       GAsyncResult *res,
                       gpointer user_data)
{
  FontViewWindow *self = FONT_VIEW_WINDOW (user_data);
  g_autoptr (GError) err = NULL;

  g_task_propagate_boolean (G_TASK (res), &err);

  if (err != NULL) {
    font_view_window_show_error (self,
                                 _("Could Not Install Font"),
                                 err->message);
  }
  install_button_refresh_appearance (self, err);

  g_clear_object (&self->cancellable);
}

static void
install_font_job (GTask *task,
                  gpointer source_object,
                  gpointer user_data,
                  GCancellable *cancellable)
{
  GFile *dest_location = user_data;
  FontViewWindow *self = FONT_VIEW_WINDOW (source_object);
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
action_install_font_cb (GtkWidget  *widget,
                        const char *action_name,
                        GVariant   *parameter)
{
  FontViewWindow *self = FONT_VIEW_WINDOW (widget);

  g_autoptr (GError) err = NULL;
  g_autoptr (GFile) home_prefix = NULL, xdg_prefix = NULL;
  g_autoptr (GFile) xdg_location = NULL, home_location = NULL,
                    dest_location = NULL;
  g_autoptr (GTask) task = NULL;
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
    g_warning ("Install failed: can't find any configured user font directory.");
    return;
  }

  if (!g_file_query_exists (dest_location, NULL)) {
    g_file_make_directory_with_parents (dest_location, NULL, &err);
    if (err) {
      install_button_refresh_appearance (self, err);
      font_view_window_show_error (self,
                                   _("Could Not Install Font"),
                                   err->message);
      return;
    }
  }

  self->cancellable = g_cancellable_new ();

  task = g_task_new (self, self->cancellable, font_install_finished, self);
  g_task_set_task_data (task, g_object_ref (dest_location), g_object_unref);
  g_task_run_in_thread (task, install_font_job);

  install_button_refresh_appearance (self, NULL);
}

static void
view_child_activated_cb (FontViewWindow *self,
                         guint           position,
                         GtkGridView    *grid_view)
{
  GtkSelectionModel *model = gtk_grid_view_get_model (grid_view);
  FontViewModelItem *item = FONT_VIEW_MODEL_ITEM (
      g_list_model_get_item (G_LIST_MODEL (model), position));
  GFile *font_file;
  gint face_index;

  font_file = font_view_model_item_get_font_file (item);
  face_index = font_view_model_item_get_face_index (item);

  if (font_file != NULL) {
    font_view_window_show_preview (self, font_file, face_index);
  }
}

static void
font_model_items_changed_cb (GListModel *model,
                             guint       position,
                             guint       removed,
                             guint       added,
                             gpointer    user_data)
{
  FontViewWindow *self = FONT_VIEW_WINDOW (user_data);

  if (self->font_file != NULL)
      install_button_refresh_appearance (self, NULL);
}

static void
font_widget_loaded_cb (FontViewWindow  *self,
                       SushiFontWidget *font_widget)
{
  FT_Face face = sushi_font_widget_get_ft_face (font_widget);
  const gchar *uri;

  if (face == NULL)
    return;

  uri = sushi_font_widget_get_uri (font_widget);
  g_clear_object (&self->font_file);
  self->font_file = g_file_new_for_uri (uri);

  if (face->family_name) {
    adw_window_title_set_title (self->font_title, face->family_name);
  } else {
    g_autofree gchar *basename = g_file_get_basename (self->font_file);
    adw_window_title_set_title (self->font_title, basename);
  }

  adw_window_title_set_subtitle (self->font_title, face->style_name);

  load_font_info (self);

  install_button_refresh_appearance (self, NULL);
}

static void
font_widget_error_cb (FontViewWindow  *self,
                      GError          *error,
                      SushiFontWidget *font_widget)
{
  font_view_window_show_overview (self);
  font_view_window_show_error (self, _("Could Not Display Font"), error->message);
}

FontViewWindow *
font_view_window_new (FontViewApplication *app)
{
  return g_object_new (FONT_VIEW_TYPE_WINDOW,
                       "application", app,
                       NULL);
}

void
font_view_window_show_overview (FontViewWindow *self)
{
  g_clear_object (&self->font_file);

  adw_navigation_view_pop (self->nav_view);
}

static void
action_focus_search_cb (GtkWidget  *widget,
                         const char *action_name,
                         GVariant   *parameter)
{
  FontViewWindow *self = FONT_VIEW_WINDOW (widget);

  gtk_widget_grab_focus (GTK_WIDGET (self->search_entry));
}

static void
action_clear_search_cb (GtkWidget  *widget,
                        const char *action_name,
                        GVariant   *parameter)
{
  FontViewWindow *self = FONT_VIEW_WINDOW (widget);

  gtk_editable_set_text (GTK_EDITABLE (self->search_entry), "");
}

void
font_view_window_show_preview (FontViewWindow *self,
                               GFile          *file,
                               int             face_index)
{
  g_autofree char *uri = g_file_get_uri (file);

  /* SushiFontWidget currently does not like for any of its properties to be
   * null on construction. Thus, we need to create it lazily.
   *
   * TODO: Refactor SushiFontWidget so that it can be included in the template.
   * */
  if (self->font_widget == NULL) {
    self->font_widget = sushi_font_widget_new (uri, face_index);

    gtk_widget_set_margin_bottom (GTK_WIDGET (self->font_widget), 12);
    gtk_widget_set_margin_start (GTK_WIDGET (self->font_widget), 12);
    gtk_widget_set_margin_end (GTK_WIDGET (self->font_widget), 12);
    gtk_widget_set_vexpand (GTK_WIDGET (self->font_widget), TRUE);

    gtk_viewport_set_child (self->viewport_preview, GTK_WIDGET (self->font_widget));

    g_signal_connect_swapped (self->font_widget, "loaded",
                              G_CALLBACK (font_widget_loaded_cb), self);
    g_signal_connect_swapped (self->font_widget, "error",
                              G_CALLBACK (font_widget_error_cb), self);
  } else {
      g_object_set (self->font_widget,
                    "uri", uri,
                    "face-index", face_index,
                    NULL);
      sushi_font_widget_load (SUSHI_FONT_WIDGET (self->font_widget));
  }

  gtk_toggle_button_set_active (self->info_button, FALSE);
  adw_navigation_view_push_by_tag (self->nav_view, "preview");
}

void
font_view_window_show_error (FontViewWindow *self,
                             const char     *heading,
                             const char     *body)
{
  adw_alert_dialog_set_heading (self->error_dialog, heading);
  adw_alert_dialog_set_body (self->error_dialog, heading);
  adw_dialog_present (ADW_DIALOG (self->error_dialog), GTK_WIDGET (self));
}

static void
font_view_window_dispose (GObject *object)
{
  FontViewWindow *self = FONT_VIEW_WINDOW (object);

  gtk_widget_dispose_template (GTK_WIDGET (self), FONT_VIEW_TYPE_WINDOW);

  g_cancellable_cancel (self->cancellable);

  g_clear_object (&self->cancellable);
  g_clear_object (&self->font_file);
  g_clear_object (&self->model);

  G_OBJECT_CLASS (font_view_window_parent_class)->dispose (object);
}

static void
font_view_window_class_init (FontViewWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = font_view_window_dispose;

  g_type_ensure (FONT_TYPE_INSCRIPTION);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/font-viewer/font-view-window.ui");

  gtk_widget_class_bind_template_child (widget_class, FontViewWindow, nav_view);
  gtk_widget_class_bind_template_child (widget_class, FontViewWindow, search_entry);
  gtk_widget_class_bind_template_child (widget_class, FontViewWindow, grid_view);
  gtk_widget_class_bind_template_child (widget_class, FontViewWindow, font_title);
  gtk_widget_class_bind_template_child (widget_class, FontViewWindow, install_button);
  gtk_widget_class_bind_template_child (widget_class, FontViewWindow, info_button);
  gtk_widget_class_bind_template_child (widget_class, FontViewWindow, preview_stack);
  gtk_widget_class_bind_template_child (widget_class, FontViewWindow, swin_preview);
  gtk_widget_class_bind_template_child (widget_class, FontViewWindow, viewport_preview);
  gtk_widget_class_bind_template_child (widget_class, FontViewWindow, swin_info);
  gtk_widget_class_bind_template_child (widget_class, FontViewWindow, error_dialog);

  gtk_widget_class_bind_template_child (widget_class, FontViewWindow, sort_model);

  gtk_widget_class_bind_template_callback (widget_class, font_widget_loaded_cb);
  gtk_widget_class_bind_template_callback (widget_class, view_child_activated_cb);

  gtk_widget_class_bind_template_callback (widget_class, font_name_closure);

  gtk_widget_class_install_action (widget_class, "win.focus-search", NULL, action_focus_search_cb);
  gtk_widget_class_install_action (widget_class, "win.clear-search", NULL, action_clear_search_cb);
  gtk_widget_class_install_action (widget_class, "win.install-font", NULL, action_install_font_cb);
}

static void
font_view_window_init (FontViewWindow *self)
{
  g_type_ensure (SUSHI_TYPE_FONT_WIDGET);

  gtk_widget_init_template (GTK_WIDGET (self));

  self->model = font_view_model_new ();
  g_signal_connect (font_view_model_get_list_model (self->model),
                    "items-changed", G_CALLBACK (font_model_items_changed_cb),
                    self);

  gtk_sort_list_model_set_model (self->sort_model,
                                 font_view_model_get_list_model (self->model));

  if (g_str_has_suffix (APPLICATION_ID, "Devel"))
    gtk_widget_add_css_class (GTK_WIDGET (self), "devel");

  /* Workaround: Make the search bar expandable */
  gtk_widget_set_hexpand (gtk_widget_get_parent (GTK_WIDGET (self->search_entry)), TRUE);
}
