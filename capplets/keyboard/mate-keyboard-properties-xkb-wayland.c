/* Native Wayland keyboard layout editor.  This deliberately does not use
 * libxklavier: that library requires an X display even when it is only used
 * to enumerate xkeyboard-config data. */
#include <config.h>

#include <gio/gio.h>
#include <glib/gi18n.h>
#include <xkbcommon/xkbregistry.h>

#include "capplet-util.h"
#include "mate-keyboard-properties-xkb.h"

#define XKB_GENERAL_SCHEMA "org.mate.peripherals-keyboard-xkb.general"
#define XKB_KBD_SCHEMA "org.mate.peripherals-keyboard-xkb.kbd"
#define COL_DESCRIPTION 0
#define COL_ID 1

typedef struct {
	GtkBuilder *builder;
	GSettings *kbd;
	GSettings *general;
	struct rxkb_context *registry;
} WaylandXkb;

static gchar *
layout_id (struct rxkb_layout *layout)
{
	const char *variant = rxkb_layout_get_variant (layout);
	return variant ? g_strdup_printf ("%s\t%s", rxkb_layout_get_name (layout), variant)
	               : g_strdup (rxkb_layout_get_name (layout));
}

static const gchar *
layout_description (WaylandXkb *data, const gchar *id)
{
	struct rxkb_layout *layout;

	for (layout = rxkb_layout_first (data->registry); layout; layout = rxkb_layout_next (layout)) {
		g_autofree gchar *candidate = layout_id (layout);
		if (g_strcmp0 (candidate, id) == 0)
			return rxkb_layout_get_description (layout);
	}
	return id;
}

static void
fill_selected (WaylandXkb *data)
{
	GtkTreeView *view = GTK_TREE_VIEW (gtk_builder_get_object (data->builder, "xkb_layouts_selected"));
	GtkListStore *store = GTK_LIST_STORE (gtk_tree_view_get_model (view));
	g_auto(GStrv) layouts = g_settings_get_strv (data->kbd, "layouts");
	GtkTreeIter iter;

	gtk_list_store_clear (store);
	if (!layouts[0]) {
		g_settings_set_strv (data->kbd, "layouts", (const gchar * const[]) { "us", NULL });
		return;
	}
	for (guint i = 0; layouts[i]; i++) {
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, COL_DESCRIPTION, layout_description (data, layouts[i]),
		                    COL_ID, layouts[i], -1);
	}
}

static void
save_store (WaylandXkb *data)
{
	GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (gtk_builder_get_object (data->builder, "xkb_layouts_selected")));
	GPtrArray *values = g_ptr_array_new_with_free_func (g_free);
	GtkTreeIter iter;

	if (gtk_tree_model_get_iter_first (model, &iter)) {
		do {
			gchar *id = NULL;
			gtk_tree_model_get (model, &iter, COL_ID, &id, -1);
			g_ptr_array_add (values, id);
		} while (gtk_tree_model_iter_next (model, &iter));
	}
	g_ptr_array_add (values, NULL);
	g_settings_set_strv (data->kbd, "layouts", (const gchar * const *) values->pdata);
	g_ptr_array_free (values, TRUE);
}

static gboolean
selected_iter (WaylandXkb *data, GtkTreeModel **model, GtkTreeIter *iter)
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (
		gtk_builder_get_object (data->builder, "xkb_layouts_selected")));
	return gtk_tree_selection_get_selected (selection, model, iter);
}

static void
add_layout (GtkButton *button G_GNUC_UNUSED, WaylandXkb *data)
{
	GtkWidget *dialog = gtk_dialog_new_with_buttons (_("Add Layout"),
		GTK_WINDOW (gtk_builder_get_object (data->builder, "keyboard_dialog")),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, _("Cancel"), GTK_RESPONSE_CANCEL,
		_("Add"), GTK_RESPONSE_OK, NULL);
	GtkListStore *store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
	GtkWidget *view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
	GtkTreeIter iter;
	struct rxkb_layout *layout;

	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view), -1, _("Layout"), renderer, "text", COL_DESCRIPTION, NULL);
	for (layout = rxkb_layout_first (data->registry); layout; layout = rxkb_layout_next (layout)) {
		g_autofree gchar *id = layout_id (layout);
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, COL_DESCRIPTION, rxkb_layout_get_description (layout), COL_ID, id, -1);
	}
	GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_widget_set_size_request (scroll, 520, 420);
	gtk_container_add (GTK_CONTAINER (scroll), view);
	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), scroll, TRUE, TRUE, 0);
	gtk_widget_show_all (dialog);
	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
		GtkTreeModel *model;
		GtkTreeIter chosen;
		if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (view)), &model, &chosen)) {
			gchar *id = NULL, *description = NULL;
			GtkListStore *selected = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (
				gtk_builder_get_object (data->builder, "xkb_layouts_selected"))));
			gtk_tree_model_get (model, &chosen, COL_DESCRIPTION, &description, COL_ID, &id, -1);
			gtk_list_store_append (selected, &iter);
			gtk_list_store_set (selected, &iter, COL_DESCRIPTION, description, COL_ID, id, -1);
			save_store (data);
			g_free (description); g_free (id);
		}
	}
	g_object_unref (store);
	gtk_widget_destroy (dialog);
}

static void
remove_layout (GtkButton *button G_GNUC_UNUSED, WaylandXkb *data)
{
	GtkTreeModel *model; GtkTreeIter iter;
	if (selected_iter (data, &model, &iter) && gtk_tree_model_iter_n_children (model, NULL) > 1) {
		gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
		save_store (data);
	}
}

static void
move_layout (WaylandXkb *data, gboolean up)
{
	GtkTreeModel *model; GtkTreeIter iter, other;
	if (!selected_iter (data, &model, &iter)) return;
	other = iter;
	if ((up && gtk_tree_model_iter_previous (model, &other)) ||
	    (!up && gtk_tree_model_iter_next (model, &other))) {
		gtk_list_store_swap (GTK_LIST_STORE (model), &iter, &other);
		save_store (data);
	}
}

static void move_up (GtkButton *b G_GNUC_UNUSED, WaylandXkb *d) { move_layout (d, TRUE); }
static void move_down (GtkButton *b G_GNUC_UNUSED, WaylandXkb *d) { move_layout (d, FALSE); }

static void
reset (GtkButton *button G_GNUC_UNUSED, WaylandXkb *data)
{
	g_settings_reset (data->kbd, "model");
	g_settings_reset (data->kbd, "layouts");
	g_settings_reset (data->kbd, "options");
	g_settings_reset (data->general, "default-group");
	fill_selected (data);
}

static void
destroy (GtkWidget *widget G_GNUC_UNUSED, WaylandXkb *data)
{
	rxkb_context_unref (data->registry);
	g_object_unref (data->kbd);
	g_object_unref (data->general);
	g_free (data);
}

gboolean
setup_xkb_tabs_wayland (GtkBuilder *builder)
{
	WaylandXkb *data = g_new0 (WaylandXkb, 1);
	GtkTreeView *view;
	GtkCellRenderer *renderer;
	GtkListStore *store;

	data->builder = builder;
	data->kbd = g_settings_new (XKB_KBD_SCHEMA);
	data->general = g_settings_new (XKB_GENERAL_SCHEMA);
	data->registry = rxkb_context_new (RXKB_CONTEXT_NO_FLAGS);
	if (!data->registry || !rxkb_context_parse_default_ruleset (data->registry)) {
		if (data->registry) rxkb_context_unref (data->registry);
		g_object_unref (data->kbd); g_object_unref (data->general); g_free (data);
		return FALSE;
	}

	view = GTK_TREE_VIEW (gtk_builder_get_object (builder, "xkb_layouts_selected"));
	store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
	gtk_tree_view_set_model (view, GTK_TREE_MODEL (store));
	g_object_unref (store);
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_insert_column_with_attributes (view, -1, _("Layout"), renderer, "text", COL_DESCRIPTION, NULL);
	fill_selected (data);

	gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "xkb_layouts_show")));
	{
		g_autofree gchar *model = g_settings_get_string (data->kbd, "model");
		gtk_button_set_label (GTK_BUTTON (gtk_builder_get_object (builder, "xkb_model_pick")),
		                      model[0] ? model : "pc105");
	}
	gtk_widget_set_sensitive (GTK_WIDGET (gtk_builder_get_object (builder, "xkb_model_pick")), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (gtk_builder_get_object (builder, "xkb_layout_options")), FALSE);
	g_settings_bind (data->general, "group-per-window", gtk_builder_get_object (builder, "chk_separate_group_per_window"), "active", G_SETTINGS_BIND_DEFAULT);
	g_signal_connect (gtk_builder_get_object (builder, "xkb_layouts_add"), "clicked", G_CALLBACK (add_layout), data);
	g_signal_connect (gtk_builder_get_object (builder, "xkb_layouts_remove"), "clicked", G_CALLBACK (remove_layout), data);
	g_signal_connect (gtk_builder_get_object (builder, "xkb_layouts_move_up"), "clicked", G_CALLBACK (move_up), data);
	g_signal_connect (gtk_builder_get_object (builder, "xkb_layouts_move_down"), "clicked", G_CALLBACK (move_down), data);
	g_signal_connect (gtk_builder_get_object (builder, "xkb_reset_to_defaults"), "clicked", G_CALLBACK (reset), data);
	g_signal_connect (gtk_builder_get_object (builder, "keyboard_dialog"), "destroy", G_CALLBACK (destroy), data);
	return TRUE;
}
