#include "wayland-display.h"
#include <string.h>

static void
mate_wayland_mode_free (MateWaylandMode *mode)
{
    if (!mode)
        return;

    if (mode->mode)
        zwlr_output_mode_v1_destroy(mode->mode);

    g_free(mode);
}

static void
mate_wayland_output_free (MateWaylandOutput *output)
{
    if (!output)
        return;

    if (output->head)
        zwlr_output_head_v1_destroy(output->head);

    g_free(output->name);
    g_free(output->description);
    g_list_free_full(output->modes, (GDestroyNotify) mate_wayland_mode_free);
    g_free(output);
}

static void output_mode_size (void *data, struct zwlr_output_mode_v1 *mode, int32_t width, int32_t height) {
    MateWaylandMode *m = data;
    m->width = width;
    m->height = height;
}

static void output_mode_refresh (void *data, struct zwlr_output_mode_v1 *mode, int32_t refresh) {
    MateWaylandMode *m = data;
    m->refresh = refresh;
}

static void output_mode_preferred (void *data, struct zwlr_output_mode_v1 *mode) {
    MateWaylandMode *m = data;
    m->preferred = TRUE;
}

static void output_mode_finished (void *data, struct zwlr_output_mode_v1 *mode) {
    MateWaylandMode *m = data;
    if (m->output) {
        if (m->output->current_mode == m)
            m->output->current_mode = NULL;
        m->output->modes = g_list_remove(m->output->modes, m);
    }
    m->mode = NULL;
    mate_wayland_mode_free(m);
}

static const struct zwlr_output_mode_v1_listener mode_listener = {
    .size = output_mode_size,
    .refresh = output_mode_refresh,
    .preferred = output_mode_preferred,
    .finished = output_mode_finished,
};

static void head_name(void *data, struct zwlr_output_head_v1 *head, const char *name) {
    MateWaylandOutput *output = data;
    g_free(output->name);
    output->name = g_strdup(name);
}

static void head_description(void *data, struct zwlr_output_head_v1 *head, const char *description) {
    MateWaylandOutput *output = data;
    g_free(output->description);
    output->description = g_strdup(description);
}

static void head_physical_size(void *data, struct zwlr_output_head_v1 *head, int32_t width, int32_t height) {
    MateWaylandOutput *output = data;
    output->phys_width = width;
    output->phys_height = height;
}

static void head_mode(void *data, struct zwlr_output_head_v1 *head, struct zwlr_output_mode_v1 *mode) {
    MateWaylandOutput *output = data;
    MateWaylandMode *m = g_new0(MateWaylandMode, 1);
    m->output = output;
    m->mode = mode;
    zwlr_output_mode_v1_add_listener(mode, &mode_listener, m);
    output->modes = g_list_append(output->modes, m);
}

static void head_enabled(void *data, struct zwlr_output_head_v1 *head, int32_t enabled) {
    MateWaylandOutput *output = data;
    output->enabled = enabled;
}

static void head_current_mode(void *data, struct zwlr_output_head_v1 *head, struct zwlr_output_mode_v1 *mode) {
    MateWaylandOutput *output = data;
    GList *l;
    for (l = output->modes; l != NULL; l = l->next) {
        MateWaylandMode *m = l->data;
        if (m->mode == mode) {
            output->current_mode = m;
            output->width = m->width;
            output->height = m->height;
            break;
        }
    }
}

static void head_position(void *data, struct zwlr_output_head_v1 *head, int32_t x, int32_t y) {
    MateWaylandOutput *output = data;
    output->x = x;
    output->y = y;
}

static void head_transform(void *data, struct zwlr_output_head_v1 *head, int32_t transform) {
    MateWaylandOutput *output = data;
    output->transform = transform;
}

static void head_scale(void *data, struct zwlr_output_head_v1 *head, wl_fixed_t scale) {
    MateWaylandOutput *output = data;
    output->scale = wl_fixed_to_double(scale);
}

static void head_finished(void *data, struct zwlr_output_head_v1 *head) {
    MateWaylandOutput *output = data;

    if (output->display)
        output->display->outputs = g_list_remove (output->display->outputs, output);

    output->head = NULL;
    mate_wayland_output_free(output);
}

static const struct zwlr_output_head_v1_listener head_listener = {
    .name = head_name,
    .description = head_description,
    .physical_size = head_physical_size,
    .mode = head_mode,
    .enabled = head_enabled,
    .current_mode = head_current_mode,
    .position = head_position,
    .transform = head_transform,
    .scale = head_scale,
    .finished = head_finished,
};

static void manager_head(void *data, struct zwlr_output_manager_v1 *manager, struct zwlr_output_head_v1 *head) {
    MateWaylandDisplay *display = data;
    MateWaylandOutput *output = g_new0(MateWaylandOutput, 1);
    output->display = display;
    output->head = head;
    output->scale = 1.0;
    zwlr_output_head_v1_add_listener(head, &head_listener, output);
    display->outputs = g_list_append(display->outputs, output);
}

static void manager_done(void *data, struct zwlr_output_manager_v1 *manager, uint32_t serial) {
    MateWaylandDisplay *display = data;
    GList *l;
    gboolean have_primary = FALSE;

    display->serial = serial;

    for (l = display->outputs; l != NULL; l = l->next) {
        MateWaylandOutput *output = l->data;

        output->primary = FALSE;
    }

    for (l = display->outputs; l != NULL; l = l->next) {
        MateWaylandOutput *output = l->data;

        if (output->enabled && output->x == 0 && output->y == 0) {
            output->primary = TRUE;
            have_primary = TRUE;
            break;
        }
    }

    if (!have_primary && display->outputs) {
        MateWaylandOutput *output = display->outputs->data;
        output->primary = TRUE;
    }
}

static void manager_finished(void *data, struct zwlr_output_manager_v1 *manager) {
    MateWaylandDisplay *display = data;

    if (display->output_manager == manager)
        display->output_manager = NULL;

    zwlr_output_manager_v1_destroy(manager);
}

static const struct zwlr_output_manager_v1_listener manager_listener = {
    .head = manager_head,
    .done = manager_done,
    .finished = manager_finished,
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    MateWaylandDisplay *display = data;
    if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0) {
        display->output_manager = wl_registry_bind(registry, name, &zwlr_output_manager_v1_interface, 1);
        zwlr_output_manager_v1_add_listener(display->output_manager, &manager_listener, display);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static void configuration_succeeded (void *data, struct zwlr_output_configuration_v1 *config) {
    zwlr_output_configuration_v1_destroy(config);
}

static void configuration_failed (void *data, struct zwlr_output_configuration_v1 *config) {
    g_warning ("Wayland display configuration failed");
    zwlr_output_configuration_v1_destroy(config);
}

static void configuration_cancelled (void *data, struct zwlr_output_configuration_v1 *config) {
    g_warning ("Wayland display configuration was cancelled");
    zwlr_output_configuration_v1_destroy(config);
}

static const struct zwlr_output_configuration_v1_listener configuration_listener = {
    .succeeded = configuration_succeeded,
    .failed = configuration_failed,
    .cancelled = configuration_cancelled,
};

MateWaylandDisplay *mate_wayland_display_new (void) {
    MateWaylandDisplay *display = g_new0(MateWaylandDisplay, 1);
    display->display = wl_display_connect(NULL);
    if (!display->display) {
        g_free(display);
        return NULL;
    }
    
    display->registry = wl_display_get_registry(display->display);
    wl_registry_add_listener(display->registry, &registry_listener, display);
    wl_display_roundtrip(display->display); /* Wait for globals */
    wl_display_roundtrip(display->display); /* Wait for output manager to emit events */
    
    return display;
}

void mate_wayland_display_free (MateWaylandDisplay *display) {
    if (!display)
        return;

    g_list_free_full(display->outputs, (GDestroyNotify) mate_wayland_output_free);
    display->outputs = NULL;

    if (display->output_manager) {
        zwlr_output_manager_v1_destroy(display->output_manager);
        display->output_manager = NULL;
    }
    if (display->registry) {
        wl_registry_destroy(display->registry);
    }
    if (display->display) {
        wl_display_disconnect(display->display);
    }
    g_free(display);
}

void mate_wayland_display_apply (MateWaylandDisplay *display) {
    if (!display->output_manager) return;
    struct zwlr_output_configuration_v1 *config = zwlr_output_manager_v1_create_configuration(display->output_manager, display->serial);
    zwlr_output_configuration_v1_add_listener(config, &configuration_listener, display);
    
    GList *l;
    for (l = display->outputs; l != NULL; l = l->next) {
        MateWaylandOutput *output = l->data;
        if (output->enabled) {
            struct zwlr_output_configuration_head_v1 *config_head = zwlr_output_configuration_v1_enable_head(config, output->head);
            if (output->current_mode) {
                zwlr_output_configuration_head_v1_set_mode(config_head, output->current_mode->mode);
            }
            zwlr_output_configuration_head_v1_set_position(config_head, output->x, output->y);
            zwlr_output_configuration_head_v1_set_transform(config_head, output->transform);
            zwlr_output_configuration_head_v1_set_scale(config_head, wl_fixed_from_double(output->scale));
        } else {
            zwlr_output_configuration_v1_disable_head(config, output->head);
        }
    }
    
    zwlr_output_configuration_v1_apply(config);
    wl_display_flush(display->display);
    wl_display_roundtrip(display->display);
}
