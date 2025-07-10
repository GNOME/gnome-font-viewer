/*
 * Copyright © 2022 Benjamin Otte
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
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Benjamin Otte <otte@gnome.org>
 *          Khalid Abu Shawarib <kas@gnome.org>
 */

#include "font-inscription.h"

#include <gtk/gtk.h>

#define BASELINE_FRACTION 0.7
#define INSCRIPTION_WIDTH 220

/**
 * FontInscription:
 *
 * Shows a font preview in a fixed-size area with a consistent text baseline.
 */

struct _FontInscription
{
    GtkWidget parent_instance;

    char *text;
    PangoAttrList *attrs;

    PangoLayout *layout;
};

enum
{
  PROP_0,
  PROP_ATTRIBUTES,
  PROP_TEXT,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (FontInscription, font_inscription, GTK_TYPE_WIDGET)

static GParamSpec *properties[N_PROPS] = { NULL, };

static void
font_inscription_dispose (GObject *object)
{
    FontInscription *self = FONT_INSCRIPTION (object);

    g_clear_pointer (&self->text, g_free);
    g_clear_object (&self->layout);

    G_OBJECT_CLASS (font_inscription_parent_class)->dispose (object);
}

static void
font_inscription_get_property (GObject    *object,
                               guint       property_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  FontInscription *self = FONT_INSCRIPTION (object);

  switch (property_id)
    {
        case PROP_ATTRIBUTES:
            g_value_set_boxed (value, self->attrs);
            break;

        case PROP_TEXT:
            g_value_set_string (value, self->text);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
font_inscription_set_property (GObject      *object,
                               guint         property_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    FontInscription *self = FONT_INSCRIPTION (object);

    switch (property_id)
    {
        case PROP_ATTRIBUTES:
            font_inscription_set_attributes (self, g_value_get_boxed (value));
            break;

        case PROP_TEXT:
            font_inscription_set_text (self, g_value_get_string (value));
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static PangoFontMetrics *
font_inscription_get_font_metrics (FontInscription *self)
{
    PangoContext *context = gtk_widget_get_pango_context (GTK_WIDGET (self));

    return pango_context_get_metrics (context, NULL, NULL);
}

static int
get_line_pixels (FontInscription *self,
                 int             *baseline)
{
    PangoFontMetrics *metrics = font_inscription_get_font_metrics (self);
    int ascent = pango_font_metrics_get_ascent (metrics);
    int descent = pango_font_metrics_get_descent (metrics);

    pango_font_metrics_unref (metrics);

    if (baseline)
        *baseline = ascent;

    return ascent + descent;
}

static void
font_inscription_measure_height (FontInscription *self,
                                 int             *minimum,
                                 int             *natural,
                                 int             *minimum_baseline,
                                 int             *natural_baseline)
{
    int line_pixels, baseline;

    line_pixels = get_line_pixels (self, &baseline);

    *minimum = line_pixels;
    *natural = line_pixels;

    if (minimum_baseline)
        *minimum_baseline = baseline;
    if (natural_baseline)
        *natural_baseline = baseline;
}

static void
font_inscription_measure (GtkWidget      *widget,
                         GtkOrientation  orientation,
                         int             for_size,
                         int            *minimum,
                         int            *natural,
                         int            *minimum_baseline,
                         int            *natural_baseline)
{
    FontInscription *self = FONT_INSCRIPTION (widget);

    if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        *minimum = INSCRIPTION_WIDTH * PANGO_SCALE;
        *natural = INSCRIPTION_WIDTH * PANGO_SCALE;
    }
    else
        font_inscription_measure_height (self, minimum, natural, minimum_baseline, natural_baseline);

    *minimum = PANGO_PIXELS_CEIL (*minimum);
    *natural = PANGO_PIXELS_CEIL (*natural);
    if (*minimum_baseline > 0)
        *minimum_baseline = PANGO_PIXELS_CEIL (*minimum_baseline);
    if (*natural_baseline > 0)
        *natural_baseline = PANGO_PIXELS_CEIL (*natural_baseline);
}

GskTransform *
font_inscription_get_layout_location (FontInscription *self)
{
    GtkWidget *widget = GTK_WIDGET (self);
    const int widget_width = gtk_widget_get_width (widget);
    const int widget_height = gtk_widget_get_height (widget);
    PangoRectangle ink;
    int layout_baseline = pango_layout_get_baseline (self->layout) / PANGO_SCALE;
    float effective_height = layout_baseline + (1 - BASELINE_FRACTION) * widget_height;
    float factor = 1.0, x, y;
    GskTransform *transform = NULL;

    pango_layout_get_pixel_extents (self->layout, &ink, NULL);

    if (ink.width > widget_width ||
        effective_height > widget_height)
    {
        factor = MIN ((float) widget_width / (float) ink.width,
                      (float) widget_height / (float) effective_height);
    }

    x = floor (0.5 * (widget_width - ink.width * factor) - ink.x * factor);
    y = floor (BASELINE_FRACTION * widget_height - layout_baseline * factor);

    transform = gsk_transform_translate (transform, &GRAPHENE_POINT_INIT (x, y));
    transform = gsk_transform_scale (transform, factor, factor);

    return transform;
}

static void
font_inscription_allocate (GtkWidget *widget,
                           int        width,
                           int        height,
                           int        baseline)
{
    FontInscription *self = FONT_INSCRIPTION (widget);

    pango_layout_set_width (self->layout, width * PANGO_SCALE);
    pango_layout_set_height (self->layout, -1);
}

static void
font_inscription_snapshot (GtkWidget   *widget,
                           GtkSnapshot *snapshot)
{
    FontInscription *self = FONT_INSCRIPTION (widget);

    if (!self->text || (*self->text == '\0'))
        return;

    const int widget_width = gtk_widget_get_width (widget);
    const int widget_height = gtk_widget_get_height (widget);
    GskTransform *layout_transform;
    GdkRGBA color;

    gtk_snapshot_push_clip (snapshot,
                            &GRAPHENE_RECT_INIT (0, 0,
                                                 widget_width, widget_height));
    layout_transform = font_inscription_get_layout_location (self);

    gtk_snapshot_save (snapshot);
    gtk_snapshot_transform (snapshot, layout_transform);
    gtk_widget_get_color (widget, &color);
    gtk_snapshot_append_layout (snapshot, self->layout, &color);
    gtk_snapshot_restore (snapshot);

    gtk_snapshot_pop (snapshot);
}

static void
font_inscription_class_init (FontInscriptionClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    gobject_class->dispose = font_inscription_dispose;
    gobject_class->get_property = font_inscription_get_property;
    gobject_class->set_property = font_inscription_set_property;

    widget_class->measure = font_inscription_measure;
    widget_class->size_allocate = font_inscription_allocate;
    widget_class->snapshot = font_inscription_snapshot;

    /**
    * FontInscription:attributes:
    *
    * A list of style attributes to apply to the text.
    */
    properties[PROP_ATTRIBUTES] =
        g_param_spec_boxed ("attributes", NULL, NULL,
                            PANGO_TYPE_ATTR_LIST,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    /**
    * FontInscription:text:
    *
    * The displayed text.
    */
    properties[PROP_TEXT] =
        g_param_spec_string ("text", NULL, NULL,
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (gobject_class, N_PROPS, properties);

    gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_PRESENTATION);
}

static void
font_inscription_init (FontInscription *self)
{
    self->layout = gtk_widget_create_pango_layout (GTK_WIDGET (self), NULL);
}

/**
 * font_inscription_set_text:
 * @self: a `FontInscription`
 * @text: (nullable): The text to display
 *
 * Sets the text to be displayed.
 */
void
font_inscription_set_text (FontInscription *self,
                           const char      *text)
{
    g_return_if_fail (FONT_IS_INSCRIPTION (self));

    if (!g_set_str (&self->text, text))
        return;

    pango_layout_set_text (self->layout, self->text ? self->text : "", -1);

    gtk_widget_queue_draw (GTK_WIDGET (self));

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT]);
}

/**
 * font_inscription_get_text:
 * @self: a `FontInscription`
 *
 * Gets the text that is displayed.
 *
 * Returns: (nullable) (transfer none): The displayed text
 */
const char *
font_inscription_get_text (FontInscription *self)
{
    g_return_val_if_fail (FONT_IS_INSCRIPTION (self), NULL);

    return self->text;
}

/**
 * font_inscription_set_attributes:
 * @self: a `FontInscription`
 * @attrs: (nullable): a [struct@Pango.AttrList]
 *
 * Apply attributes to the inscription text.
 */
void
font_inscription_set_attributes (FontInscription *self,
                                 PangoAttrList   *attrs)
{
    g_return_if_fail (FONT_IS_INSCRIPTION (self));

    if (self->attrs == attrs)
        return;

    if (attrs)
        pango_attr_list_ref (attrs);

    if (self->attrs)
        pango_attr_list_unref (self->attrs);
    self->attrs = attrs;

    pango_layout_set_attributes (self->layout, self->attrs);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ATTRIBUTES]);

    gtk_widget_queue_draw (GTK_WIDGET (self));
}

/**
 * font_inscription_get_attributes:
 * @self: a `FontInscription`
 *
 * Gets the inscription's attribute list.
 *
 * Returns: (nullable) (transfer none): the attribute list
 */
PangoAttrList *
font_inscription_get_attributes (FontInscription *self)
{
    g_return_val_if_fail (FONT_IS_INSCRIPTION (self), NULL);

    return self->attrs;
}

/**
 * font_inscription_new:
 * @text: (nullable): The text to display.
 * @attributes: (nullable): The text attributes.
 *
 * Creates a new `FontInscription` with the given text and attributes.
 *
 * Returns: a new `FontInscription`
 */
GtkWidget *
font_inscription_new (const char    *text,
                      PangoAttrList *attributes)
{
    return g_object_new (FONT_TYPE_INSCRIPTION,
                         "text", text,
                         "attributes", attributes,
                         NULL);
}