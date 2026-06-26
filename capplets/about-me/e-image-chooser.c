/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-image-chooser.c
 * Copyright (C) 2004  Novell, Inc.
 * Author: Chris Toshok <toshok@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <config.h>

#include <math.h>
#include <string.h>
#include <unistd.h>

#include <glib-object.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#if HAVE_GSTREAMER
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#endif

#include "e-image-chooser.h"

#ifndef MATECC_FACES_DIR
#define MATECC_FACES_DIR DATADIR "/pixmaps/faces"
#endif
#ifndef MATECC_SOURCE_FACES_DIR
#define MATECC_SOURCE_FACES_DIR "data/faces"
#endif

#define PREVIEW_SIZE 100
#define SELECTOR_PREVIEW_SIZE 112
#define FACE_THUMB_SIZE 72
#define CAMERA_WIDTH 640
#define CAMERA_HEIGHT 480

typedef struct _EImageChooserPrivate EImageChooserPrivate;
struct _EImageChooserPrivate {

	GtkWidget *preview;
	GdkPixbuf *pixbuf;

	char *image_buf;
	int   image_buf_size;
	int   width;
	int   height;
	char *fallback_name;

	gboolean editable;
	gboolean scaleable;
};

enum {
	CHANGED,
	LAST_SIGNAL
};

enum {
	PROP_0,
	PROP_WIDTH,
	PROP_HEIGHT,
	NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES] = { NULL, };
static guint image_chooser_signals [LAST_SIGNAL] = { 0 };

static void e_image_chooser_dispose      (GObject *object);

static gboolean image_drag_motion_cb (GtkWidget *widget,
				      GdkDragContext *context,
				      gint x, gint y, guint time, EImageChooser *chooser);
static gboolean image_drag_drop_cb (GtkWidget *widget,
				    GdkDragContext *context,
				    gint x, gint y, guint time, EImageChooser *chooser);
static void image_drag_data_received_cb (GtkWidget *widget,
					 GdkDragContext *context,
					 gint x, gint y,
					 GtkSelectionData *selection_data,
					 guint info, guint time, EImageChooser *chooser);

G_DEFINE_TYPE_WITH_PRIVATE (EImageChooser, e_image_chooser, GTK_TYPE_BOX);

enum DndTargetType {
	DND_TARGET_TYPE_URI_LIST
};
#define URI_LIST_TYPE "text/uri-list"

static GtkTargetEntry image_drag_types[] = {
	{ URI_LIST_TYPE, 0, DND_TARGET_TYPE_URI_LIST },
};
static const int num_image_drag_types = sizeof (image_drag_types) / sizeof (image_drag_types[0]);

GtkWidget *
e_image_chooser_new (void)
{
	return g_object_new (E_TYPE_IMAGE_CHOOSER, "orientation", GTK_ORIENTATION_VERTICAL, NULL);
}

GtkWidget *e_image_chooser_new_with_size (int width, int height)
{
	return g_object_new (E_TYPE_IMAGE_CHOOSER,
			"width", width,
			"height", height,
			"orientation", GTK_ORIENTATION_VERTICAL, NULL);
}

static void
e_image_chooser_set_property (GObject      *object,
			guint         prop_id,
			const GValue *value,
			GParamSpec   *pspec)
{
	EImageChooserPrivate *priv;

	priv = e_image_chooser_get_instance_private (E_IMAGE_CHOOSER (object));

	switch (prop_id)
	{
		case PROP_WIDTH:
			priv->width = g_value_get_int (value);
			priv->scaleable = FALSE;
			gtk_widget_set_size_request (priv->preview, priv->width, priv->height);
			break;
		case PROP_HEIGHT:
			priv->height = g_value_get_int (value);
			priv->scaleable = FALSE;
			gtk_widget_set_size_request (priv->preview, priv->width, priv->height);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
e_image_chooser_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	EImageChooserPrivate *priv;

	priv = e_image_chooser_get_instance_private (E_IMAGE_CHOOSER (object));

	switch (prop_id)
	{
		case PROP_WIDTH:
			g_value_set_int (value, priv->width);
			break;
		case PROP_HEIGHT:
			g_value_set_int (value, priv->height);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
e_image_chooser_class_init (EImageChooserClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = e_image_chooser_dispose;
	object_class->set_property = e_image_chooser_set_property;
	object_class->get_property = e_image_chooser_get_property;

	image_chooser_signals [CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (EImageChooserClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	properties[PROP_WIDTH] =
		g_param_spec_int ("width",
				"Chooser width",
				"Chooser width to show image",
				0, G_MAXINT,
				32,
				G_PARAM_READWRITE);
	properties[PROP_HEIGHT] =
		g_param_spec_int ("height",
				"Chooser height",
				"Chooser height to show image",
				0, G_MAXINT,
				32,
				G_PARAM_READWRITE);

	g_object_class_install_properties (object_class, NUM_PROPERTIES, properties);
}

static GdkRGBA
get_fallback_color (const gchar *name)
{
	static const gchar *colors[] = {
		"#1b7f79", "#b44649", "#5b6fb4", "#8d5f2d",
		"#7a4a91", "#3d7d3b", "#b0622f", "#4f6f7f"
	};
	GdkRGBA color;
	guint hash;

	hash = name && *name ? g_str_hash (name) : 0;
	gdk_rgba_parse (&color, colors[hash % G_N_ELEMENTS (colors)]);

	return color;
}

static gchar *
get_fallback_initial (const gchar *name)
{
	gunichar ch = 'A';
	gchar buf[8];

	if (name && *name)
		ch = g_utf8_get_char (name);

	ch = g_unichar_toupper (ch);
	memset (buf, 0, sizeof (buf));
	g_unichar_to_utf8 (ch, buf);

	return g_strdup (buf);
}

static void
draw_circular_pixbuf (cairo_t *cr,
		      GdkPixbuf *pixbuf,
		      gint width,
		      gint height)
{
	gint src_width, src_height;
	gdouble scale, draw_width, draw_height;
	gdouble x, y, radius;

	src_width = gdk_pixbuf_get_width (pixbuf);
	src_height = gdk_pixbuf_get_height (pixbuf);
	scale = MAX ((gdouble) width / src_width, (gdouble) height / src_height);
	draw_width = src_width * scale;
	draw_height = src_height * scale;
	x = (width - draw_width) / 2.0;
	y = (height - draw_height) / 2.0;
	radius = MIN (width, height) / 2.0;

	cairo_save (cr);
	cairo_arc (cr, width / 2.0, height / 2.0, radius, 0, 2 * G_PI);
	cairo_clip (cr);
	cairo_translate (cr, x, y);
	cairo_scale (cr, scale, scale);
	gdk_cairo_set_source_pixbuf (cr, pixbuf, 0, 0);
	cairo_paint (cr);
	cairo_restore (cr);
}

static gboolean
preview_draw_cb (GtkWidget *widget, cairo_t *cr, EImageChooser *chooser)
{
	EImageChooserPrivate *priv;
	GtkAllocation allocation;
	gint size;

	priv = e_image_chooser_get_instance_private (chooser);
	gtk_widget_get_allocation (widget, &allocation);
	size = MIN (allocation.width, allocation.height);

	cairo_translate (cr, (allocation.width - size) / 2.0, (allocation.height - size) / 2.0);

	if (priv->pixbuf != NULL) {
		draw_circular_pixbuf (cr, priv->pixbuf, size, size);
	} else {
		GdkRGBA color;
		gchar *initial;
		PangoLayout *layout;
		PangoFontDescription *font;
		int text_width, text_height;

		color = get_fallback_color (priv->fallback_name);
		gdk_cairo_set_source_rgba (cr, &color);
		cairo_arc (cr, size / 2.0, size / 2.0, size / 2.0, 0, 2 * G_PI);
		cairo_fill (cr);

		initial = get_fallback_initial (priv->fallback_name);
		layout = gtk_widget_create_pango_layout (widget, initial);
		font = pango_font_description_new ();
		pango_font_description_set_weight (font, PANGO_WEIGHT_BOLD);
		pango_font_description_set_absolute_size (font, size * PANGO_SCALE * 0.42);
		pango_layout_set_font_description (layout, font);
		pango_layout_get_pixel_size (layout, &text_width, &text_height);

		cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
		cairo_move_to (cr, (size - text_width) / 2.0, (size - text_height) / 2.0);
		pango_cairo_show_layout (cr, layout);

		pango_font_description_free (font);
		g_object_unref (layout);
		g_free (initial);
	}

	return FALSE;
}

static GtkWidget *
create_avatar_preview (EImageChooser *chooser, gint size)
{
	GtkWidget *preview;

	preview = gtk_drawing_area_new ();
	gtk_widget_set_size_request (preview, size, size);
	g_signal_connect (preview, "draw", G_CALLBACK (preview_draw_cb), chooser);

	return preview;
}

static void
e_image_chooser_init (EImageChooser *chooser)
{
	EImageChooserPrivate *priv;

	priv = e_image_chooser_get_instance_private (chooser);

	priv->width = PREVIEW_SIZE;
	priv->height = PREVIEW_SIZE;
	priv->preview = create_avatar_preview (chooser, PREVIEW_SIZE);
	priv->scaleable = TRUE;

	gtk_box_set_homogeneous (GTK_BOX (chooser), FALSE);
	gtk_box_pack_start (GTK_BOX (chooser), priv->preview, TRUE, TRUE, 0);

	gtk_drag_dest_set (priv->preview, 0, image_drag_types, num_image_drag_types, GDK_ACTION_COPY);
	g_signal_connect (priv->preview,
			  "drag_motion", G_CALLBACK (image_drag_motion_cb), chooser);
	g_signal_connect (priv->preview,
			  "drag_drop", G_CALLBACK (image_drag_drop_cb), chooser);
	g_signal_connect (priv->preview,
			  "drag_data_received", G_CALLBACK (image_drag_data_received_cb), chooser);

	gtk_widget_show_all (priv->preview);

	priv->editable = TRUE;
}

static void
e_image_chooser_dispose (GObject *object)
{
	EImageChooserPrivate *priv;

	priv = e_image_chooser_get_instance_private (E_IMAGE_CHOOSER (object));

	g_clear_object (&priv->pixbuf);
	g_clear_pointer (&priv->image_buf, g_free);
	g_clear_pointer (&priv->fallback_name, g_free);

	if (G_OBJECT_CLASS (e_image_chooser_parent_class)->dispose)
		(* G_OBJECT_CLASS (e_image_chooser_parent_class)->dispose) (object);
}

static gboolean
set_image_from_data (EImageChooser *chooser,
		     char *data, int length)
{
	gboolean rv = FALSE;
	GdkPixbufLoader *loader = gdk_pixbuf_loader_new ();
	GdkPixbuf *pixbuf;
	EImageChooserPrivate *priv;

	priv = e_image_chooser_get_instance_private (chooser);

	gdk_pixbuf_loader_write (loader, (guchar *) data, length, NULL);
	gdk_pixbuf_loader_close (loader, NULL);

	pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
	if (pixbuf)
		g_object_ref (pixbuf);
	g_object_unref (loader);

	if (pixbuf) {
		g_clear_object (&priv->pixbuf);
		priv->pixbuf = pixbuf;

		g_free (priv->image_buf);
		priv->image_buf = data;
		priv->image_buf_size = length;

		gtk_widget_queue_draw (priv->preview);

		g_signal_emit (chooser,
			       image_chooser_signals [CHANGED], 0);

		rv = TRUE;
	}

	return rv;
}

static gboolean
image_drag_motion_cb (GtkWidget *widget,
		      GdkDragContext *context,
		      gint x, gint y, guint time, EImageChooser *chooser)
{
	GList *p;
	EImageChooserPrivate *priv;

	priv = e_image_chooser_get_instance_private (chooser);

	if (!priv->editable)
		return FALSE;

	for (p = gdk_drag_context_list_targets (context); p; p = p->next) {
		char *possible_type;

		possible_type = gdk_atom_name (GDK_POINTER_TO_ATOM (p->data));
		if (!strcmp (possible_type, URI_LIST_TYPE)) {
			g_free (possible_type);
			gdk_drag_status (context, GDK_ACTION_COPY, time);
			return TRUE;
		}

		g_free (possible_type);
	}

	return FALSE;
}

static gboolean
image_drag_drop_cb (GtkWidget *widget,
		    GdkDragContext *context,
		    gint x, gint y, guint time, EImageChooser *chooser)
{
	GList *p;
	EImageChooserPrivate *priv;

	priv = e_image_chooser_get_instance_private (chooser);

	if (!priv->editable)
		return FALSE;

	if (gdk_drag_context_list_targets (context) == NULL) {
		return FALSE;
	}

	for (p = gdk_drag_context_list_targets (context); p; p = p->next) {
		char *possible_type;

		possible_type = gdk_atom_name (GDK_POINTER_TO_ATOM (p->data));
		if (!strcmp (possible_type, URI_LIST_TYPE)) {
			g_free (possible_type);
			gtk_drag_get_data (widget, context,
					   GDK_POINTER_TO_ATOM (p->data),
					   time);
			return TRUE;
		}

		g_free (possible_type);
	}

	return FALSE;
}

static void
image_drag_data_received_cb (GtkWidget *widget,
			     GdkDragContext *context,
			     gint x, gint y,
			     GtkSelectionData *selection_data,
			     guint info, guint time, EImageChooser *chooser)
{
	char *target_type;
	gboolean handled = FALSE;

	target_type = gdk_atom_name (gtk_selection_data_get_target (selection_data));

	if (!strcmp (target_type, URI_LIST_TYPE)) {
		const char *data = (const char *) gtk_selection_data_get_data (selection_data);
		char *uri;
		GFile *file;
		GInputStream *istream;
		const char *nl = strstr (data, "\r\n");

		if (nl)
			uri = g_strndup (data, nl - data);
		else
			uri = g_strdup (data);

		file = g_file_new_for_uri (uri);
		istream = G_INPUT_STREAM (g_file_read (file, NULL, NULL));

		if (istream != NULL) {
			GFileInfo *info;

			info = g_file_query_info (file,
						  G_FILE_ATTRIBUTE_STANDARD_SIZE,
						  G_FILE_QUERY_INFO_NONE,
						  NULL, NULL);

			if (info != NULL) {
				gsize size;
				gboolean success;
				gchar *buf;

				size = g_file_info_get_size (info);
				g_object_unref (info);

				buf = g_malloc (size);

				success = g_input_stream_read_all (istream,
								   buf,
								   size,
								   &size,
								   NULL,
								   NULL);
				g_input_stream_close (istream, NULL, NULL);

				if (success &&
						set_image_from_data (chooser, buf, size))
					handled = TRUE;
				else
					g_free (buf);
			}

			g_object_unref (istream);
		}

		g_object_unref (file);
		g_free (uri);
	}

	g_free (target_type);
	gtk_drag_finish (context, handled, FALSE, time);
}

gboolean
e_image_chooser_set_from_file (EImageChooser *chooser, const char *filename)
{
	gchar *data;
	gsize data_length;

	g_return_val_if_fail (E_IS_IMAGE_CHOOSER (chooser), FALSE);
	g_return_val_if_fail (filename, FALSE);

	if (!g_file_get_contents (filename, &data, &data_length, NULL)) {
		return FALSE;
	}

	if (!set_image_from_data (chooser, data, data_length))
		g_free (data);

	return TRUE;
}

void
e_image_chooser_set_fallback_name (EImageChooser *chooser, const char *name)
{
	EImageChooserPrivate *priv;

	g_return_if_fail (E_IS_IMAGE_CHOOSER (chooser));

	priv = e_image_chooser_get_instance_private (chooser);

	g_free (priv->fallback_name);
	priv->fallback_name = g_strdup (name);
	gtk_widget_queue_draw (priv->preview);
}

void
e_image_chooser_set_editable (EImageChooser *chooser, gboolean editable)
{
	EImageChooserPrivate *priv;

	g_return_if_fail (E_IS_IMAGE_CHOOSER (chooser));

	priv = e_image_chooser_get_instance_private (chooser);

	priv->editable = editable;
}

void
e_image_chooser_set_scaleable  (EImageChooser *chooser, gboolean scaleable)
{
	EImageChooserPrivate *priv;

	g_return_if_fail (E_IS_IMAGE_CHOOSER (chooser));

	priv = e_image_chooser_get_instance_private (chooser);

	priv->scaleable = scaleable;
}

gboolean
e_image_chooser_get_image_data (EImageChooser *chooser, char **data, gsize *data_length)
{
	EImageChooserPrivate *priv;

	g_return_val_if_fail (E_IS_IMAGE_CHOOSER (chooser), FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (data_length != NULL, FALSE);

	priv = e_image_chooser_get_instance_private (chooser);

	if (priv->image_buf == NULL || priv->image_buf_size <= 0)
		return FALSE;

	*data_length = priv->image_buf_size;
	*data = g_malloc (*data_length);
	memcpy (*data, priv->image_buf, *data_length);

	return TRUE;
}

gboolean
e_image_chooser_set_image_data (EImageChooser *chooser, char *data, gsize data_length)
{
	char *buf;

	g_return_val_if_fail (E_IS_IMAGE_CHOOSER (chooser), FALSE);
	g_return_val_if_fail (data != NULL, FALSE);

	buf = g_malloc (data_length);
	memcpy (buf, data, data_length);

	if (!set_image_from_data (chooser, buf, data_length)) {
		g_free (buf);
		return FALSE;
	}

	return TRUE;
}

typedef struct {
	EImageChooser *chooser;
	GtkWidget *dialog;
	gchar *filename;
} FaceButtonData;

static gboolean
face_thumbnail_draw_cb (GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
	GdkPixbuf *pixbuf;
	GtkAllocation allocation;
	gint size;

	pixbuf = g_object_get_data (G_OBJECT (widget), "face-pixbuf");
	if (pixbuf == NULL)
		return FALSE;

	gtk_widget_get_allocation (widget, &allocation);
	size = MIN (allocation.width, allocation.height);
	cairo_translate (cr,
			 (allocation.width - size) / 2.0,
			 (allocation.height - size) / 2.0);
	draw_circular_pixbuf (cr, pixbuf, size, size);

	return FALSE;
}

static void
face_button_data_free (FaceButtonData *data)
{
	g_free (data->filename);
	g_free (data);
}

static gboolean
is_face_file (const gchar *filename)
{
	gchar *lower;
	gboolean is_face;

	lower = g_ascii_strdown (filename, -1);
	is_face = g_str_has_suffix (lower, ".jpg") ||
		  g_str_has_suffix (lower, ".jpeg") ||
		  g_str_has_suffix (lower, ".png") ||
		  g_str_has_suffix (lower, ".svg");
	g_free (lower);

	return is_face;
}

static gchar *
get_faces_dir (void)
{
	static const gchar *dirs[] = {
		MATECC_FACES_DIR,
		MATECC_SOURCE_FACES_DIR,
		"data/faces",
		"../../data/faces"
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (dirs); i++) {
		if (g_file_test (dirs[i], G_FILE_TEST_IS_DIR))
			return g_strdup (dirs[i]);
	}

	g_warning ("No profile face directory found. Tried: %s, %s, %s, %s",
		   dirs[0], dirs[1], dirs[2], dirs[3]);

	return NULL;
}

static GtkWidget *
create_face_button (EImageChooser *chooser, GtkWidget *dialog, const gchar *filename)
{
	GtkWidget *button;
	GtkWidget *thumbnail;
	GdkPixbuf *pixbuf;
	FaceButtonData *data;
	gchar *basename;
	GError *error = NULL;

	pixbuf = gdk_pixbuf_new_from_file_at_scale (filename,
						    FACE_THUMB_SIZE,
						    FACE_THUMB_SIZE,
						    TRUE,
						    &error);
	if (pixbuf == NULL) {
		g_warning ("Could not load profile face '%s': %s",
			   filename, error ? error->message : "unknown error");
		g_clear_error (&error);
		return NULL;
	}

	button = gtk_button_new ();
	gtk_widget_set_size_request (button, FACE_THUMB_SIZE + 16, FACE_THUMB_SIZE + 16);
	basename = g_path_get_basename (filename);
	gtk_widget_set_tooltip_text (button, basename);
	g_free (basename);
	thumbnail = gtk_drawing_area_new ();
	gtk_widget_set_size_request (thumbnail, FACE_THUMB_SIZE, FACE_THUMB_SIZE);
	g_object_set_data_full (G_OBJECT (thumbnail),
				"face-pixbuf",
				pixbuf,
				g_object_unref);
	g_signal_connect (thumbnail, "draw",
			  G_CALLBACK (face_thumbnail_draw_cb), NULL);
	gtk_container_add (GTK_CONTAINER (button), thumbnail);

	data = g_new0 (FaceButtonData, 1);
	data->chooser = chooser;
	data->dialog = dialog;
	data->filename = g_strdup (filename);
	g_object_set_data_full (G_OBJECT (button), "face-button-data", data,
				(GDestroyNotify) face_button_data_free);

	return button;
}

static void
face_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	FaceButtonData *data;

	data = g_object_get_data (G_OBJECT (button), "face-button-data");
	if (data == NULL)
		return;

	e_image_chooser_set_from_file (data->chooser, data->filename);
	gtk_dialog_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_ACCEPT);
}

static void
populate_faces_grid (EImageChooser *chooser, GtkWidget *dialog, GtkWidget *flowbox)
{
	GDir *dir;
	const gchar *name;
	GList *files = NULL, *l;
	gchar *faces_dir;
	guint loaded = 0;
	GError *error = NULL;

	faces_dir = get_faces_dir ();
	if (faces_dir == NULL)
		return;

	dir = g_dir_open (faces_dir, 0, &error);
	if (dir == NULL) {
		g_warning ("Could not open profile face directory '%s': %s",
			   faces_dir, error ? error->message : "unknown error");
		g_clear_error (&error);
		g_free (faces_dir);
		return;
	}

	while ((name = g_dir_read_name (dir)) != NULL) {
		if (is_face_file (name))
			files = g_list_prepend (files, g_build_filename (faces_dir, name, NULL));
	}
	g_dir_close (dir);

	if (files == NULL)
		g_warning ("Profile face directory '%s' contains no supported images", faces_dir);

	files = g_list_sort (files, (GCompareFunc) g_strcmp0);
	for (l = files; l != NULL; l = l->next) {
		GtkWidget *button;

		button = create_face_button (chooser, dialog, l->data);
		if (button != NULL) {
			g_signal_connect (button, "clicked",
					  G_CALLBACK (face_button_clicked_cb), NULL);
			gtk_flow_box_insert (GTK_FLOW_BOX (flowbox), button, -1);
			loaded++;
		}
	}

	if (loaded == 0 && files != NULL)
		g_warning ("No profile faces from '%s' could be loaded by GdkPixbuf", faces_dir);

	gtk_widget_show_all (flowbox);
	g_list_free_full (files, g_free);
	g_free (faces_dir);
}

static void
select_file_clicked_cb (GtkButton *button, gpointer user_data)
{
	EImageChooser *chooser = E_IMAGE_CHOOSER (user_data);
	GtkWidget *selector;
	GtkWidget *file_dialog;
	GtkFileFilter *filter;
	const gchar *pics_dir;
	gchar *faces_dir;
	gint response;

	selector = gtk_widget_get_toplevel (GTK_WIDGET (button));
	file_dialog = gtk_file_chooser_dialog_new (_("Select Image"),
						  GTK_WINDOW (selector),
						  GTK_FILE_CHOOSER_ACTION_OPEN,
						  "gtk-cancel", GTK_RESPONSE_CANCEL,
						  "gtk-open", GTK_RESPONSE_ACCEPT,
						  NULL);
	gtk_window_set_modal (GTK_WINDOW (file_dialog), TRUE);
	gtk_dialog_set_default_response (GTK_DIALOG (file_dialog), GTK_RESPONSE_ACCEPT);

	faces_dir = get_faces_dir ();
	if (faces_dir != NULL)
		gtk_file_chooser_add_shortcut_folder (GTK_FILE_CHOOSER (file_dialog),
						      faces_dir, NULL);

	pics_dir = g_get_user_special_dir (G_USER_DIRECTORY_PICTURES);
	if (pics_dir != NULL)
		gtk_file_chooser_add_shortcut_folder (GTK_FILE_CHOOSER (file_dialog),
						      pics_dir, NULL);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("Images"));
	gtk_file_filter_add_pixbuf_formats (filter);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_dialog), filter);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All Files"));
	gtk_file_filter_add_pattern (filter, "*");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_dialog), filter);

	response = gtk_dialog_run (GTK_DIALOG (file_dialog));
	if (response == GTK_RESPONSE_ACCEPT) {
		gchar *filename;

		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_dialog));
		if (e_image_chooser_set_from_file (chooser, filename))
			gtk_dialog_response (GTK_DIALOG (selector), GTK_RESPONSE_ACCEPT);
		g_free (filename);
	}

	gtk_widget_destroy (file_dialog);
	g_free (faces_dir);
}

static gchar *
get_video_device (void)
{
#if HAVE_GSTREAMER
	gint i;

	for (i = 0; i < 10; i++) {
		gchar *path;

		path = g_strdup_printf ("/dev/video%d", i);
		if (g_file_test (path, G_FILE_TEST_EXISTS)) {
			if (g_access (path, R_OK) == 0)
				return path;

			g_warning ("Camera device '%s' exists but is not readable", path);
		}
		g_free (path);
	}
#endif

	return NULL;
}

static gboolean
has_video_device (void)
{
	gchar *device;

	device = get_video_device ();
	if (device == NULL)
		return FALSE;

	g_free (device);
	return TRUE;
}

#if HAVE_GSTREAMER
typedef struct {
	gint ref_count;
	EImageChooser *chooser;
	GtkWidget *dialog;
	GtkWidget *stack;
	GtkWidget *video_box;
	GtkWidget *video_widget;
	GtkWidget *area;
	GtkWidget *guide;
	GtkWidget *capture_button;
	GtkWidget *again_button;
	GtkWidget *done_button;
	GstElement *pipeline;
	GstElement *capture_sink;
	guint bus_watch_id;
	GdkPixbuf *live_pixbuf;
	GdkPixbuf *captured_pixbuf;
	gchar *device;
	gboolean running;
	gboolean review;
} CameraSession;

typedef struct {
	CameraSession *session;
	GdkPixbuf *pixbuf;
} CameraFrameUpdate;

static CameraSession *
camera_session_ref (CameraSession *session)
{
	g_atomic_int_inc (&session->ref_count);
	return session;
}

static void
camera_session_unref (CameraSession *session)
{
	if (g_atomic_int_dec_and_test (&session->ref_count)) {
		g_clear_object (&session->pipeline);
		g_clear_object (&session->capture_sink);
		g_clear_pointer (&session->device, g_free);
		g_clear_object (&session->live_pixbuf);
		g_clear_object (&session->captured_pixbuf);
		g_free (session);
	}
}

static gboolean
camera_guide_draw_cb (GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
	GtkAllocation allocation;
	gdouble width, height, radius, cx, cy;

	gtk_widget_get_allocation (widget, &allocation);
	width = allocation.width;
	height = allocation.height;
	cx = width / 2.0;
	cy = height / 2.0;
	radius = MIN (width, height) * 0.36;

	cairo_save (cr);
	cairo_set_fill_rule (cr, CAIRO_FILL_RULE_EVEN_ODD);
	cairo_rectangle (cr, 0, 0, width, height);
	cairo_arc (cr, cx, cy, radius, 0, 2 * G_PI);
	cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.42);
	cairo_fill (cr);
	cairo_restore (cr);

	cairo_save (cr);
	cairo_arc (cr, cx, cy, radius + 2.0, 0, 2 * G_PI);
	cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.88);
	cairo_set_line_width (cr, 3.0);
	cairo_stroke (cr);

	cairo_arc (cr, cx, cy, radius - 12.0, 0, 2 * G_PI);
	cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.22);
	cairo_set_line_width (cr, 1.0);
	cairo_stroke (cr);

	cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.75);
	cairo_set_line_width (cr, 2.0);
	cairo_move_to (cr, cx, cy - radius - 20.0);
	cairo_line_to (cr, cx, cy - radius + 18.0);
	cairo_move_to (cr, cx, cy + radius - 18.0);
	cairo_line_to (cr, cx, cy + radius + 20.0);
	cairo_move_to (cr, cx - radius - 20.0, cy);
	cairo_line_to (cr, cx - radius + 18.0, cy);
	cairo_move_to (cr, cx + radius - 18.0, cy);
	cairo_line_to (cr, cx + radius + 20.0, cy);
	cairo_stroke (cr);
	cairo_restore (cr);

	return FALSE;
}

static gboolean
camera_area_draw_cb (GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
	CameraSession *session = user_data;
	GtkAllocation allocation;
	GdkPixbuf *pixbuf;
	gint width, height;

	gtk_widget_get_allocation (widget, &allocation);
	width = allocation.width;
	height = allocation.height;
	pixbuf = session->review ? session->captured_pixbuf : session->live_pixbuf;

	cairo_set_source_rgb (cr, 0.08, 0.09, 0.10);
	cairo_rectangle (cr, 0, 0, width, height);
	cairo_fill (cr);

	if (pixbuf != NULL)
		draw_circular_pixbuf (cr, pixbuf, width, height);
	else {
		PangoLayout *layout;
		int text_width, text_height;

		layout = gtk_widget_create_pango_layout (widget, _("Camera Feed"));
		pango_layout_get_pixel_size (layout, &text_width, &text_height);
		cairo_set_source_rgb (cr, 0.85, 0.85, 0.85);
		cairo_move_to (cr, (width - text_width) / 2.0, (height - text_height) / 2.0);
		pango_cairo_show_layout (cr, layout);
		g_object_unref (layout);
	}

	camera_guide_draw_cb (widget, cr, user_data);

	return FALSE;
}

static void
camera_update_buttons (CameraSession *session)
{
	gtk_widget_set_visible (session->capture_button, !session->review);
	gtk_widget_set_visible (session->again_button, session->review);
	gtk_widget_set_visible (session->done_button, session->review);

	if (session->stack != NULL) {
		gtk_stack_set_visible_child_name (GTK_STACK (session->stack),
						  session->review ? "review" : "live");
	}
}

static gboolean
camera_frame_idle_cb (gpointer user_data)
{
	CameraFrameUpdate *update = user_data;
	CameraSession *session = update->session;

	if (session->running && update->pixbuf != NULL && !session->review) {
		g_clear_object (&session->live_pixbuf);
		session->live_pixbuf = g_object_ref (update->pixbuf);
		gtk_widget_queue_draw (session->area);
	}

	g_clear_object (&update->pixbuf);
	camera_session_unref (session);
	g_free (update);

	return G_SOURCE_REMOVE;
}

static GstFlowReturn
camera_new_sample_cb (GstElement *appsink, gpointer user_data)
{
	CameraSession *session = user_data;
	GstSample *sample;
	GstBuffer *buffer;
	GstMapInfo map;
	GstCaps *caps;
	GstVideoInfo info;
	GBytes *bytes;
	GdkPixbuf *pixbuf;
	CameraFrameUpdate *update;

	if (!session->running)
		return GST_FLOW_OK;

	sample = gst_app_sink_pull_sample (GST_APP_SINK (appsink));
	if (sample == NULL)
		return GST_FLOW_OK;

	caps = gst_sample_get_caps (sample);
	if (!gst_video_info_from_caps (&info, caps)) {
		gst_sample_unref (sample);
		return GST_FLOW_OK;
	}

	buffer = gst_sample_get_buffer (sample);
	if (!gst_buffer_map (buffer, &map, GST_MAP_READ)) {
		gst_sample_unref (sample);
		return GST_FLOW_OK;
	}

	bytes = g_bytes_new (map.data, map.size);
	pixbuf = gdk_pixbuf_new_from_bytes (bytes,
					    GDK_COLORSPACE_RGB,
					    FALSE,
					    8,
					    GST_VIDEO_INFO_WIDTH (&info),
					    GST_VIDEO_INFO_HEIGHT (&info),
					    GST_VIDEO_INFO_PLANE_STRIDE (&info, 0));
	g_bytes_unref (bytes);
	gst_buffer_unmap (buffer, &map);
	gst_sample_unref (sample);

	update = g_new0 (CameraFrameUpdate, 1);
	update->session = camera_session_ref (session);
	update->pixbuf = pixbuf;
	g_idle_add (camera_frame_idle_cb, update);

	return GST_FLOW_OK;
}

static gboolean
camera_bus_message_cb (GstBus *bus, GstMessage *message, gpointer user_data)
{
	CameraSession *session = user_data;

	switch (GST_MESSAGE_TYPE (message)) {
	case GST_MESSAGE_ERROR: {
		GError *error = NULL;
		gchar *debug = NULL;

		gst_message_parse_error (message, &error, &debug);
		g_warning ("Camera pipeline error from %s: %s%s%s",
			   GST_OBJECT_NAME (message->src),
			   error ? error->message : "unknown error",
			   debug ? " / " : "",
			   debug ? debug : "");
		g_clear_error (&error);
		g_free (debug);

		session->running = FALSE;
		if (session->pipeline != NULL)
			gst_element_set_state (session->pipeline, GST_STATE_NULL);
		break;
	}
	case GST_MESSAGE_WARNING: {
		GError *error = NULL;
		gchar *debug = NULL;

		gst_message_parse_warning (message, &error, &debug);
		g_warning ("Camera pipeline warning from %s: %s%s%s",
			   GST_OBJECT_NAME (message->src),
			   error ? error->message : "unknown warning",
			   debug ? " / " : "",
			   debug ? debug : "");
		g_clear_error (&error);
		g_free (debug);
		break;
	}
	case GST_MESSAGE_STATE_CHANGED:
		if (GST_MESSAGE_SRC (message) == GST_OBJECT (session->pipeline)) {
			GstState old_state, new_state, pending_state;

			gst_message_parse_state_changed (message, &old_state, &new_state, &pending_state);
			g_debug ("Camera pipeline state changed: %s -> %s",
				 gst_element_state_get_name (old_state),
				 gst_element_state_get_name (new_state));
		}
		break;
	default:
		break;
	}

	return G_SOURCE_CONTINUE;
}

static const gchar *
get_gtk_video_sink_name (void)
{
	GstElementFactory *factory;

	factory = gst_element_factory_find ("gtksink");
	if (factory != NULL) {
		gst_object_unref (factory);
		return "gtksink";
	}

	factory = gst_element_factory_find ("gtkglsink");
	if (factory != NULL) {
		gst_object_unref (factory);
		return "gtkglsink";
	}

	return NULL;
}

static gboolean
camera_start (CameraSession *session)
{
	GError *error = NULL;
	GstBus *bus;
	GstElement *video_sink;
	GstStateChangeReturn state_ret;
	const gchar *gtk_sink_name;
	gchar *source_desc;
	gchar *pipeline_desc;

	if (!gst_is_initialized ())
		gst_init (NULL, NULL);

	gtk_sink_name = get_gtk_video_sink_name ();
	if (gtk_sink_name == NULL) {
		g_warning ("No GTK GStreamer video sink found. Install the plugin providing gtksink or gtkglsink.");
		return FALSE;
	}

	{
		GstElementFactory *factory;

		factory = gst_element_factory_find ("v4l2src");
		if (factory == NULL)
			g_warning ("GStreamer v4l2src plugin is not available; camera devices may not open.");
		else
			gst_object_unref (factory);
	}

	if (session->device != NULL) {
		source_desc = g_strdup_printf ("v4l2src device=%s", session->device);
	} else {
		source_desc = g_strdup ("autovideosrc");
	}

	pipeline_desc = g_strdup_printf (
		"%s ! videoconvert ! videoscale ! tee name=t "
		"t. ! queue ! videoconvert ! %s name=videosink sync=false "
		"t. ! queue leaky=downstream max-size-buffers=1 ! videoconvert ! videoscale ! "
		"video/x-raw,format=RGB,width=%d,height=%d ! "
		"appsink name=capturesink emit-signals=true max-buffers=1 drop=true sync=false",
		source_desc,
		gtk_sink_name,
		CAMERA_WIDTH,
		CAMERA_HEIGHT);

	session->pipeline = gst_parse_launch (pipeline_desc, &error);
	g_free (pipeline_desc);
	g_free (source_desc);

	if (session->pipeline == NULL) {
		g_warning ("Could not create camera pipeline: %s",
			   error ? error->message : "unknown error");
		g_clear_error (&error);
		return FALSE;
	}

	video_sink = gst_bin_get_by_name (GST_BIN (session->pipeline), "videosink");
	if (video_sink == NULL) {
		g_warning ("Camera pipeline did not create a video sink");
		return FALSE;
	}
	g_object_get (video_sink, "widget", &session->video_widget, NULL);
	g_object_unref (video_sink);

	if (session->video_widget == NULL) {
		g_warning ("Camera video sink '%s' did not expose a GTK widget", gtk_sink_name);
		return FALSE;
	}

	gtk_widget_set_hexpand (session->video_widget, TRUE);
	gtk_widget_set_vexpand (session->video_widget, TRUE);
	gtk_container_add (GTK_CONTAINER (session->video_box), session->video_widget);
	gtk_widget_show_all (session->video_box);

	session->capture_sink = gst_bin_get_by_name (GST_BIN (session->pipeline), "capturesink");
	if (session->capture_sink == NULL) {
		g_warning ("Camera pipeline did not create a capture sink");
		return FALSE;
	}
	g_signal_connect (session->capture_sink, "new-sample",
			  G_CALLBACK (camera_new_sample_cb), session);

	session->running = TRUE;
	bus = gst_element_get_bus (session->pipeline);
	session->bus_watch_id = gst_bus_add_watch (bus, camera_bus_message_cb, session);
	gst_object_unref (bus);

	state_ret = gst_element_set_state (session->pipeline, GST_STATE_PLAYING);
	if (state_ret == GST_STATE_CHANGE_FAILURE) {
		g_warning ("Camera pipeline failed to enter PLAYING state");
		session->running = FALSE;
		return FALSE;
	}

	return TRUE;
}

static void
camera_capture_clicked_cb (GtkButton *button, gpointer user_data)
{
	CameraSession *session = user_data;

	if (session->live_pixbuf == NULL)
		return;

	g_clear_object (&session->captured_pixbuf);
	session->captured_pixbuf = g_object_ref (session->live_pixbuf);
	session->review = TRUE;
	camera_update_buttons (session);
	gtk_widget_queue_draw (session->area);
}

static void
camera_again_clicked_cb (GtkButton *button, gpointer user_data)
{
	CameraSession *session = user_data;

	g_clear_object (&session->captured_pixbuf);
	session->review = FALSE;
	camera_update_buttons (session);
	gtk_widget_queue_draw (session->area);
}

static void
camera_done_clicked_cb (GtkButton *button, gpointer user_data)
{
	CameraSession *session = user_data;
	gchar *data = NULL;
	gsize length = 0;
	GError *error = NULL;

	if (session->captured_pixbuf == NULL)
		return;

	if (gdk_pixbuf_save_to_buffer (session->captured_pixbuf,
				       &data,
				       &length,
				       "png",
				       &error,
				       "compression", "9",
				       NULL)) {
		e_image_chooser_set_image_data (session->chooser, data, length);
		gtk_widget_destroy (session->dialog);
	}
	else {
		g_warning ("Could not save camera frame: %s", error ? error->message : "unknown error");
		g_clear_error (&error);
	}

	g_free (data);
}

static void
camera_dialog_destroy_cb (GtkWidget *dialog, gpointer user_data)
{
	CameraSession *session = user_data;

	session->running = FALSE;
	if (session->bus_watch_id != 0) {
		g_source_remove (session->bus_watch_id);
		session->bus_watch_id = 0;
	}
	if (session->pipeline != NULL)
		gst_element_set_state (session->pipeline, GST_STATE_NULL);
	camera_session_unref (session);
}

static void
take_picture_clicked_cb (GtkButton *button, gpointer user_data)
{
	EImageChooser *chooser = E_IMAGE_CHOOSER (user_data);
	GtkWidget *selector;
	GtkWidget *dialog;
	GtkWidget *content;
	GtkWidget *actions;
	CameraSession *session;

	selector = gtk_widget_get_toplevel (GTK_WIDGET (button));
	dialog = gtk_dialog_new_with_buttons (_("Take a Picture"),
					     GTK_WINDOW (selector),
					     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					     "gtk-cancel", GTK_RESPONSE_CANCEL,
					     NULL);
	gtk_window_set_default_size (GTK_WINDOW (dialog), CAMERA_WIDTH, CAMERA_HEIGHT + 72);
	content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	gtk_container_set_border_width (GTK_CONTAINER (content), 12);

	session = g_new0 (CameraSession, 1);
	session->ref_count = 1;
	session->chooser = chooser;
	session->dialog = dialog;
	session->device = get_video_device ();
	session->stack = gtk_stack_new ();
	gtk_widget_set_size_request (session->stack, CAMERA_WIDTH, CAMERA_HEIGHT);

	session->video_box = gtk_overlay_new ();
	gtk_widget_set_hexpand (session->video_box, TRUE);
	gtk_widget_set_vexpand (session->video_box, TRUE);
	session->guide = gtk_drawing_area_new ();
	gtk_widget_set_halign (session->guide, GTK_ALIGN_FILL);
	gtk_widget_set_valign (session->guide, GTK_ALIGN_FILL);
	gtk_overlay_add_overlay (GTK_OVERLAY (session->video_box), session->guide);

	session->area = gtk_drawing_area_new ();
	gtk_widget_set_size_request (session->area, CAMERA_WIDTH, CAMERA_HEIGHT);

	gtk_stack_add_named (GTK_STACK (session->stack), session->video_box, "live");
	gtk_stack_add_named (GTK_STACK (session->stack), session->area, "review");
	gtk_box_pack_start (GTK_BOX (content), session->stack, TRUE, TRUE, 0);

	actions = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_button_box_set_layout (GTK_BUTTON_BOX (actions), GTK_BUTTONBOX_END);
	gtk_box_set_spacing (GTK_BOX (actions), 6);
	gtk_widget_set_margin_top (actions, 6);
	session->capture_button = gtk_button_new_with_mnemonic (_("_Capture"));
	session->again_button = gtk_button_new_with_mnemonic (_("Take _Another"));
	session->done_button = gtk_button_new_with_mnemonic (_("_Done"));
	gtk_box_pack_start (GTK_BOX (actions), session->again_button, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (actions), session->capture_button, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (actions), session->done_button, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (content), actions, FALSE, FALSE, 0);

	g_signal_connect (session->area, "draw",
			  G_CALLBACK (camera_area_draw_cb), session);
	g_signal_connect (session->guide, "draw",
			  G_CALLBACK (camera_guide_draw_cb), session);
	g_signal_connect (session->capture_button, "clicked",
			  G_CALLBACK (camera_capture_clicked_cb), session);
	g_signal_connect (session->again_button, "clicked",
			  G_CALLBACK (camera_again_clicked_cb), session);
	g_signal_connect (session->done_button, "clicked",
			  G_CALLBACK (camera_done_clicked_cb), session);
	g_signal_connect (dialog, "destroy",
			  G_CALLBACK (camera_dialog_destroy_cb), session);

	camera_update_buttons (session);
	if (!camera_start (session)) {
		gtk_widget_destroy (dialog);
		return;
	}

	gtk_widget_show_all (dialog);
	camera_update_buttons (session);
}
#endif

void
e_image_chooser_show_selector (EImageChooser *chooser, GtkWindow *parent)
{
	EImageChooserPrivate *priv;
	GtkWidget *dialog;
	GtkWidget *content;
	GtkWidget *top;
	GtkWidget *top_preview;
	GtkWidget *title_box;
	GtkWidget *label;
	GtkWidget *scrolled;
	GtkWidget *flowbox;
	GtkWidget *actions;
	GtkWidget *file_button;
	GtkWidget *camera_button;

	g_return_if_fail (E_IS_IMAGE_CHOOSER (chooser));

	priv = e_image_chooser_get_instance_private (chooser);
	if (!priv->editable)
		return;

	dialog = gtk_dialog_new_with_buttons (_("Select Profile Picture"),
					     parent,
					     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					     "gtk-close", GTK_RESPONSE_CLOSE,
					     NULL);
	gtk_window_set_default_size (GTK_WINDOW (dialog), 520, 520);

	content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	gtk_container_set_border_width (GTK_CONTAINER (content), 18);
	gtk_box_set_spacing (GTK_BOX (content), 14);

	top = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 14);
	top_preview = create_avatar_preview (chooser, SELECTOR_PREVIEW_SIZE);
	gtk_box_pack_start (GTK_BOX (top), top_preview, FALSE, FALSE, 0);

	title_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_valign (title_box, GTK_ALIGN_CENTER);
	label = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (label), _("<b>Profile Picture</b>"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (title_box), label, FALSE, FALSE, 0);
	label = gtk_label_new (_("Choose a stock avatar, select an image file, or take a picture."));
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (title_box), label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (top), title_box, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (content), top, FALSE, FALSE, 0);

	scrolled = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
					GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request (scrolled, -1, 260);
	flowbox = gtk_flow_box_new ();
	gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (flowbox), GTK_SELECTION_NONE);
	gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (flowbox), 4);
	gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (flowbox), 6);
	gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (flowbox), 8);
	gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (flowbox), 8);
	gtk_container_add (GTK_CONTAINER (scrolled), flowbox);
	gtk_box_pack_start (GTK_BOX (content), scrolled, TRUE, TRUE, 0);

	populate_faces_grid (chooser, dialog, flowbox);

	actions = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (actions, GTK_ALIGN_END);

	camera_button = gtk_button_new_with_mnemonic (_("_Take a Picture..."));
	gtk_widget_set_sensitive (camera_button, has_video_device ());
	if (!has_video_device ())
		gtk_widget_set_no_show_all (camera_button, TRUE);
#if HAVE_GSTREAMER
	g_signal_connect (camera_button, "clicked",
			  G_CALLBACK (take_picture_clicked_cb), chooser);
#endif
	gtk_box_pack_start (GTK_BOX (actions), camera_button, FALSE, FALSE, 0);

	file_button = gtk_button_new_with_mnemonic (_("_Select a File..."));
	g_signal_connect (file_button, "clicked",
			  G_CALLBACK (select_file_clicked_cb), chooser);
	gtk_box_pack_start (GTK_BOX (actions), file_button, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (content), actions, FALSE, FALSE, 0);

	gtk_widget_show_all (dialog);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}
