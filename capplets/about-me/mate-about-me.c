/* mate-about-me.c
 * Copyright (C) 2002 Diego Gonzalez
 *
 * Written by: Diego Gonzalez <diego@pemas.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib/gstdio.h>
#include <gio/gio.h>
#include <unistd.h>

#if HAVE_ACCOUNTSSERVICE
#include <act/act.h>
#endif

#include "e-image-chooser.h"
#include "mate-about-me-password.h"
#include "mate-about-me-fingerprint.h"

#include "capplet-util.h"

#define MAX_HEIGHT 100
#define MAX_WIDTH  100
#define FACE_ICON_SIZE 256

typedef struct {

	GtkBuilder 	*dialog;
	GtkWidget	*enable_fingerprint_button;
	GtkWidget	*disable_fingerprint_button;
	GtkWidget   	*image_chooser;
	GdkPixbuf       *image;
#if HAVE_ACCOUNTSSERVICE
	ActUser         *user;
#endif

	GdkScreen    	*screen;
	GtkIconTheme 	*theme;

	gboolean      	 have_image;
	gboolean      	 image_changed;
	gboolean      	 loading_image;
	gboolean      	 create_self;

	gchar        	*person;
	gchar 		*login;
	gchar 		*username;
	gchar           *pending_icon_file;
	gchar           *pending_real_name;
	gboolean         loading_real_name;
} MateAboutMe;

static MateAboutMe *me = NULL;

static GdkPixbuf *
about_me_create_face_pixbuf (GdkPixbuf *pixbuf)
{
	int width, height, size, src_x, src_y;
	GdkPixbuf *cropped;
	GdkPixbuf *scaled;

	g_return_val_if_fail (GDK_IS_PIXBUF (pixbuf), NULL);

	width = gdk_pixbuf_get_width (pixbuf);
	height = gdk_pixbuf_get_height (pixbuf);
	size = MIN (width, height);
	src_x = (width - size) / 2;
	src_y = (height - size) / 2;

	cropped = gdk_pixbuf_new_subpixbuf (pixbuf, src_x, src_y, size, size);
	scaled = gdk_pixbuf_scale_simple (cropped,
					  FACE_ICON_SIZE,
					  FACE_ICON_SIZE,
					  GDK_INTERP_BILINEAR);
	g_object_unref (cropped);

	return scaled;
}

static void
about_me_destroy (void)
{
	if (me->dialog)
		g_object_unref (me->dialog);
	if (me->image)
		g_object_unref (me->image);

	g_free (me->person);
	g_free (me->login);
	g_free (me->username);
	g_free (me->pending_icon_file);
	g_free (me->pending_real_name);
	g_free (me);
	me = NULL;
}

static void
about_me_set_accounts_icon_file (MateAboutMe *me, const gchar *file)
{
#if HAVE_ACCOUNTSSERVICE
	g_free (me->pending_icon_file);
	me->pending_icon_file = g_strdup (file);

	if (me->user != NULL && act_user_is_loaded (me->user)) {
		act_user_set_icon_file (me->user, me->pending_icon_file);
		g_clear_pointer (&me->pending_icon_file, g_free);
	}
#endif
}

static void
about_me_set_accounts_real_name (MateAboutMe *me, const gchar *real_name)
{
#if HAVE_ACCOUNTSSERVICE
	g_free (me->pending_real_name);
	me->pending_real_name = g_strdup (real_name);

	if (me->user != NULL && act_user_is_loaded (me->user)) {
		g_debug ("Updating user real name to: %s", me->pending_real_name);
		act_user_set_real_name (me->user, me->pending_real_name);
		g_clear_pointer (&me->pending_real_name, g_free);
	}
#endif
}

static void
about_me_set_real_name_mode (MateAboutMe *me, const gchar *mode)
{
	GtkWidget *stack;

	stack = GTK_WIDGET (gtk_builder_get_object (me->dialog, "realname-stack"));
	gtk_stack_set_visible_child_name (GTK_STACK (stack), mode);
}

static void
about_me_update_real_name_display (MateAboutMe *me)
{
	GtkWidget *label;
	gchar *markup;
	const gchar *real_name;

	label = GTK_WIDGET (gtk_builder_get_object (me->dialog, "fullname"));
	real_name = me->username;
	if (real_name == NULL || !g_utf8_validate (real_name, -1, NULL))
		real_name = g_get_real_name ();

	markup = g_markup_printf_escaped ("<b><span size=\"xx-large\">%s</span></b>",
					  real_name ? real_name : "");
	gtk_label_set_markup (GTK_LABEL (label), markup);
	g_free (markup);
}

static gboolean
about_me_real_name_is_valid (const gchar *real_name)
{
	const gchar *p;
	gchar *stripped;
	gboolean valid = TRUE;

	if (real_name == NULL)
		return FALSE;

	stripped = g_strdup (real_name);
	g_strstrip (stripped);
	if (*stripped == '\0')
		valid = FALSE;
	g_free (stripped);

	if (!valid)
		return FALSE;

	if (!g_utf8_validate (real_name, -1, NULL))
		return FALSE;

	for (p = real_name; *p != '\0'; p = g_utf8_next_char (p)) {
		gunichar ch;

		ch = g_utf8_get_char (p);
		if (!g_unichar_validate (ch) || g_unichar_iscntrl (ch))
			return FALSE;
	}

	return TRUE;
}

static const gchar *
about_me_get_initial_real_name (MateAboutMe *me)
{
#if HAVE_ACCOUNTSSERVICE
	const gchar *real_name;

	if (me->user != NULL && act_user_is_loaded (me->user)) {
		real_name = act_user_get_real_name (me->user);
		if (real_name != NULL && *real_name != '\0')
			return real_name;
	}
#endif

	return g_get_real_name ();
}

static void
about_me_set_real_name_entry (MateAboutMe *me, const gchar *real_name)
{
	GtkWidget *entry;
	gchar *safe_real_name;

	entry = GTK_WIDGET (gtk_builder_get_object (me->dialog, "entry-realname"));
	if (real_name != NULL && *real_name != '\0' && g_utf8_validate (real_name, -1, NULL))
		safe_real_name = g_strdup (real_name);
	else
		safe_real_name = g_strdup (g_get_real_name ());

	me->loading_real_name = TRUE;
	gtk_entry_set_text (GTK_ENTRY (entry), safe_real_name);
	me->loading_real_name = FALSE;

	g_free (me->username);
	me->username = safe_real_name;
	about_me_update_real_name_display (me);
	about_me_set_real_name_mode (me, "display");
	e_image_chooser_set_fallback_name (E_IMAGE_CHOOSER (me->image_chooser), me->username);
}

static void
about_me_commit_real_name (MateAboutMe *me)
{
	GtkWidget *entry;
	const gchar *text;
	gchar *real_name;
	gchar *title;

	if (me->loading_real_name)
		return;

	entry = GTK_WIDGET (gtk_builder_get_object (me->dialog, "entry-realname"));
	text = gtk_entry_get_text (GTK_ENTRY (entry));
	real_name = g_strdup (text);
	g_strstrip (real_name);

	if (!about_me_real_name_is_valid (real_name)) {
		gtk_style_context_add_class (gtk_widget_get_style_context (entry), GTK_STYLE_CLASS_ERROR);
		g_free (real_name);
		return;
	}

	gtk_style_context_remove_class (gtk_widget_get_style_context (entry), GTK_STYLE_CLASS_ERROR);

	if (g_strcmp0 (me->username, real_name) == 0) {
		about_me_set_real_name_mode (me, "display");
		g_free (real_name);
		return;
	}

	g_free (me->username);
	me->username = g_strdup (real_name);
	about_me_update_real_name_display (me);
	about_me_set_real_name_mode (me, "display");
	e_image_chooser_set_fallback_name (E_IMAGE_CHOOSER (me->image_chooser), me->username);

	title = g_strdup_printf (_("About %s"), me->username);
	gtk_window_set_title (GTK_WINDOW (gtk_builder_get_object (me->dialog, "about-me-dialog")), title);
	g_free (title);

	about_me_set_accounts_real_name (me, real_name);
	g_free (real_name);
}

static void
about_me_real_name_changed_cb (GtkEditable *editable, MateAboutMe *me)
{
	if (me->loading_real_name)
		return;

	gtk_style_context_remove_class (gtk_widget_get_style_context (GTK_WIDGET (editable)),
					GTK_STYLE_CLASS_ERROR);
}

static void
about_me_real_name_activate_cb (GtkEntry *entry, MateAboutMe *me)
{
	about_me_commit_real_name (me);
}

static gboolean
about_me_real_name_focus_out_cb (GtkWidget *entry, GdkEventFocus *event, MateAboutMe *me)
{
	about_me_real_name_activate_cb (GTK_ENTRY (entry), me);

	return FALSE;
}

static gboolean
about_me_real_name_display_button_press_cb (GtkWidget *widget,
					    GdkEventButton *event,
					    MateAboutMe *me)
{
	GtkWidget *entry;

	if (event->button != 1)
		return FALSE;

	entry = GTK_WIDGET (gtk_builder_get_object (me->dialog, "entry-realname"));
	gtk_style_context_remove_class (gtk_widget_get_style_context (entry), GTK_STYLE_CLASS_ERROR);
	about_me_set_real_name_mode (me, "edit");
	gtk_widget_grab_focus (entry);
	gtk_editable_select_region (GTK_EDITABLE (entry), 0, -1);

	return TRUE;
}

static void
about_me_load_photo (MateAboutMe *me)
{
	gchar         *file = NULL;
	GError        *error = NULL;
#if HAVE_ACCOUNTSSERVICE
	const gchar   *act_file;

	if (me->user != NULL && act_user_is_loaded (me->user)) {
		act_file = act_user_get_icon_file (me->user);
		if ( act_file != NULL && strlen (act_file) > 1) {
			file = g_strdup (act_file);
		}
	}
#endif
	if (file == NULL) {
		file = g_build_filename (g_get_home_dir (), ".face", NULL);
	}

	me->loading_image = TRUE;
	me->image = gdk_pixbuf_new_from_file(file, &error);

	if (me->image != NULL) {
		e_image_chooser_set_from_file (E_IMAGE_CHOOSER (me->image_chooser), file);
		me->have_image = TRUE;
	} else {
		me->have_image = FALSE;
		if (error != NULL) {
			g_warning ("Could not load %s: %s", file, error->message);
			g_error_free (error);
		}
	}
	me->loading_image = FALSE;
	g_free (file);
}

static void
about_me_update_photo (MateAboutMe *me)
{
	gchar         *file;
	GError        *error = NULL;

	guchar 	      *data;
	gsize 	       length;

	if (me->image_changed && me->have_image) {
		GdkPixbufLoader *loader = gdk_pixbuf_loader_new ();
		GdkPixbuf *pixbuf = NULL, *face = NULL;
		char *face_data = NULL;
		gsize face_length;

		if (!e_image_chooser_get_image_data (E_IMAGE_CHOOSER (me->image_chooser), (char **) &data, &length)) {
			g_warning ("Could not get selected user image data");
			return;
		}

		/* Decode the selected image, then publish a bounded square PNG. */
		gdk_pixbuf_loader_write (loader, data, length, NULL);
		gdk_pixbuf_loader_close (loader, NULL);

		pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);

		if (pixbuf)
			g_object_ref (pixbuf);

		g_object_unref (loader);

		if (pixbuf == NULL) {
			g_warning ("Could not read selected user image");
			g_free (data);
			return;
		}

		face = about_me_create_face_pixbuf (pixbuf);
		if (face == NULL ||
		    !gdk_pixbuf_save_to_buffer (face, &face_data, &face_length, "png", &error,
						"compression", "9", NULL)) {
			g_warning ("Could not prepare user image: %s", error ? error->message : "unknown error");
			g_clear_error (&error);
			g_clear_object (&face);
			g_object_unref (pixbuf);
			g_free (data);
			return;
		}

		g_free (data);
		data = (guchar *) face_data;
		length = face_length;
		g_object_unref (face);

		/* Save the image for greeters that read ~/.face directly. */
		error = NULL;
		file = g_build_filename (g_get_home_dir (), ".face", NULL);
		if (g_file_set_contents (file, (gchar *)data, length, &error) == TRUE) {
			g_chmod (file, 0644);
#if HAVE_ACCOUNTSSERVICE
			about_me_set_accounts_icon_file (me, file);
#endif
		} else {
			g_warning ("Could not create %s: %s", file, error->message);
			g_error_free (error);
		}

		g_free (file);
		g_object_unref (pixbuf);
		g_free (data);
	} else if (me->image_changed && !me->have_image) {
		/* Remove the image from greeters that read ~/.face directly. */
		file = g_build_filename (g_get_home_dir (), ".face", NULL);

		g_unlink (file);

		g_free (file);
#if HAVE_ACCOUNTSSERVICE
		about_me_set_accounts_icon_file (me, "");
#endif
	}
}

static void
about_me_load_info (MateAboutMe *me)
{
	set_fingerprint_label (me->enable_fingerprint_button,
			       me->disable_fingerprint_button);
}

static void
about_me_image_clicked_cb (GtkWidget *button, MateAboutMe *me)
{
	e_image_chooser_show_selector (E_IMAGE_CHOOSER (me->image_chooser),
				       GTK_WINDOW (gtk_builder_get_object (me->dialog, "about-me-dialog")));
}

static void
about_me_image_changed_cb (GtkWidget *widget, MateAboutMe *me)
{
	if (me->loading_image)
		return;

	me->have_image = TRUE;
	me->image_changed = TRUE;
	about_me_update_photo (me);
}

/* About Me Dialog Callbacks */

static void
about_me_icon_theme_changed (GtkWindow    *window,
			     GtkIconTheme *theme)
{
	GtkIconInfo *icon;

	icon = gtk_icon_theme_lookup_icon (me->theme, "avatar-default", 80, 0);
	if (icon != NULL) {
		g_free (me->person);
		me->person = g_strdup (gtk_icon_info_get_filename (icon));
		g_object_unref (icon);
	}

	if (!me->have_image)
		e_image_chooser_set_fallback_name (E_IMAGE_CHOOSER (me->image_chooser), me->username);
}

static void
about_me_button_clicked_cb (GtkDialog *dialog, gint response_id, MateAboutMe *me)
{
	about_me_commit_real_name (me);

	about_me_destroy ();
	gtk_main_quit ();
}

static void
about_me_passwd_clicked_cb (GtkWidget *button, MateAboutMe *me)
{
	GtkBuilder *dialog;

	dialog = me->dialog;
	mate_about_me_password (GTK_WINDOW (gtk_builder_get_object (dialog, "about-me-dialog")));
}

static void
about_me_fingerprint_button_clicked_cb (GtkWidget *button, MateAboutMe *me)
{
	fingerprint_button_clicked (me->dialog,
				    me->enable_fingerprint_button,
				    me->disable_fingerprint_button);
}

#if HAVE_ACCOUNTSSERVICE
static void on_user_is_loaded_changed (ActUser *user, GParamSpec *pspec, MateAboutMe* me)
{
	if (act_user_is_loaded (user)) {
		gboolean had_pending_icon;

		had_pending_icon = me->pending_icon_file != NULL;
		if (me->pending_icon_file != NULL) {
			act_user_set_icon_file (me->user, me->pending_icon_file);
			g_clear_pointer (&me->pending_icon_file, g_free);
		}
		if (me->pending_real_name != NULL) {
			g_debug ("Updating user real name to: %s", me->pending_real_name);
			act_user_set_real_name (me->user, me->pending_real_name);
			g_clear_pointer (&me->pending_real_name, g_free);
		} else {
			about_me_set_real_name_entry (me, about_me_get_initial_real_name (me));
		}
		if (!had_pending_icon)
			about_me_load_photo (me);
		g_signal_handlers_disconnect_by_func (G_OBJECT (user),
				G_CALLBACK (on_user_is_loaded_changed),
				me);
	}
}
#endif

static gint
about_me_setup_dialog (void)
{
	GtkWidget    *widget;
	GtkWidget    *main_dialog;
	GtkIconInfo  *icon;
	GtkBuilder   *dialog;
	GError       *error = NULL;
	gchar        *str;
#if HAVE_ACCOUNTSSERVICE
	ActUserManager* manager;
#endif

	me = g_new0 (MateAboutMe, 1);
	me->image = NULL;

	dialog = gtk_builder_new ();
	if (gtk_builder_add_from_resource (dialog, "/org/mate/mcc/am/mate-about-me-dialog.ui", &error) == 0)
        {
                g_warning ("Could not parse UI definition: %s", error->message);
                g_error_free (error);
        }

	me->image_chooser = e_image_chooser_new_with_size (MAX_WIDTH, MAX_HEIGHT);
	gtk_container_add (GTK_CONTAINER (gtk_builder_get_object (dialog, "button-image")), me->image_chooser);

	if (dialog == NULL) {
		about_me_destroy ();
		return -1;
	}

	me->dialog = dialog;

	/* Connect the close button signal */
	main_dialog = WID ("about-me-dialog");
	g_signal_connect (main_dialog, "response",
			  G_CALLBACK (about_me_button_clicked_cb), me);

	gtk_window_set_resizable (GTK_WINDOW (main_dialog), FALSE);
	capplet_set_icon (main_dialog, "user-info");

	/* Setup theme details */
	me->screen = gtk_window_get_screen (GTK_WINDOW (main_dialog));
	me->theme = gtk_icon_theme_get_for_screen (me->screen);

	icon = gtk_icon_theme_lookup_icon (me->theme, "avatar-default", 80, 0);
	if (icon != NULL) {
		me->person = g_strdup (gtk_icon_info_get_filename (icon));
		g_object_unref (icon);
	}

	g_signal_connect_object (me->theme, "changed",
				 G_CALLBACK (about_me_icon_theme_changed),
				 main_dialog,
				 G_CONNECT_SWAPPED);

	me->login = g_strdup (g_get_user_name ());
	me->username = g_strdup (about_me_get_initial_real_name (me));
	e_image_chooser_set_fallback_name (E_IMAGE_CHOOSER (me->image_chooser), me->username);

#if HAVE_ACCOUNTSSERVICE
	manager = act_user_manager_get_default ();
	me->user = act_user_manager_get_user (manager, me->login);
	if (me->user != NULL)
		g_signal_connect (me->user, "notify::is-loaded", G_CALLBACK (on_user_is_loaded_changed), me);
#endif
	/* Contact Tab */
	about_me_load_photo (me);

	widget = WID ("entry-realname");
	about_me_set_real_name_entry (me, me->username);
	g_signal_connect (widget, "changed",
			  G_CALLBACK (about_me_real_name_changed_cb), me);
	g_signal_connect (widget, "activate",
			  G_CALLBACK (about_me_real_name_activate_cb), me);
	g_signal_connect (widget, "focus-out-event",
			  G_CALLBACK (about_me_real_name_focus_out_cb), me);
	widget = WID ("realname-display-event");
	g_signal_connect (widget, "button-press-event",
			  G_CALLBACK (about_me_real_name_display_button_press_cb), me);

	widget = WID ("login");
	gtk_label_set_text (GTK_LABEL (widget), me->login);

	str = g_strdup_printf (_("About %s"), me->username);
	gtk_window_set_title (GTK_WINDOW (main_dialog), str);
	g_free (str);

	widget = WID ("password");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (about_me_passwd_clicked_cb), me);

	widget = WID ("button-image");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (about_me_image_clicked_cb), me);

	me->enable_fingerprint_button = WID ("enable_fingerprint_button");
	me->disable_fingerprint_button = WID ("disable_fingerprint_button");

	g_signal_connect (me->enable_fingerprint_button, "clicked",
			  G_CALLBACK (about_me_fingerprint_button_clicked_cb), me);
	g_signal_connect (me->disable_fingerprint_button, "clicked",
			  G_CALLBACK (about_me_fingerprint_button_clicked_cb), me);

	g_signal_connect (me->image_chooser, "changed",
			  G_CALLBACK (about_me_image_changed_cb), me);

	about_me_load_info (me);

	gtk_widget_show_all (main_dialog);

	return 0;
}

int
main (int argc, char **argv)
{
	int rc = 0;

	capplet_init (NULL, &argc, &argv);

	rc = about_me_setup_dialog ();

	if (rc != -1) {
		gtk_main ();
	}

	return rc;
}
