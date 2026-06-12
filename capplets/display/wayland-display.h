#ifndef MATE_WAYLAND_DISPLAY_H
#define MATE_WAYLAND_DISPLAY_H

#include <glib-object.h>
#include <wayland-client.h>
#include "wlr-output-management-unstable-v1-protocol.h"

G_BEGIN_DECLS

typedef struct _MateWaylandDisplay MateWaylandDisplay;
typedef struct _MateWaylandOutput MateWaylandOutput;
typedef struct _MateWaylandMode MateWaylandMode;

struct _MateWaylandOutput {
    MateWaylandDisplay *display;
    struct zwlr_output_head_v1 *head;
    char *name;
    char *description;
    int32_t x, y;
    int32_t width, height;
    int32_t phys_width, phys_height;
    int32_t transform;
    double scale;
    gboolean enabled;
    gboolean primary;
    GList *modes; /* List of MateWaylandMode */
    MateWaylandMode *current_mode;
};

struct _MateWaylandMode {
    MateWaylandOutput *output;
    struct zwlr_output_mode_v1 *mode;
    int32_t width, height;
    int32_t refresh;
    gboolean preferred;
};

struct _MateWaylandDisplay {
    struct wl_display *display;
    struct wl_registry *registry;
    struct zwlr_output_manager_v1 *output_manager;
    uint32_t serial;
    GList *outputs; /* List of MateWaylandOutput */
};

MateWaylandDisplay *mate_wayland_display_new (void);
void mate_wayland_display_free (MateWaylandDisplay *display);
void mate_wayland_display_apply (MateWaylandDisplay *display);

G_END_DECLS

#endif /* MATE_WAYLAND_DISPLAY_H */
