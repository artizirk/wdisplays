/* SPDX-FileCopyrightText: 2020 Jason Francis <jason@cycles.network>
 * SPDX-License-Identifier: GPL-3.0-or-later */

#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>

#include "hypr-displays.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define SCREEN_MARGIN_PERCENT 0.02

static void layer_surface_configure(void *data,
    struct zwlr_layer_surface_v1 *surface,
    uint32_t serial, uint32_t width, uint32_t height) {
  struct wd_output *output = data;
  gtk_widget_set_size_request(output->overlay_window, width, height);
  zwlr_layer_surface_v1_ack_configure(surface, serial);
}

static void layer_surface_closed(void *data,
    struct zwlr_layer_surface_v1 *surface) {
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
  .configure = layer_surface_configure,
  .closed = layer_surface_closed,
};

static inline int min(int a, int b) {
  return a < b ? a : b;
}

static PangoLayout *create_text_layout(struct wd_head *head,
    PangoContext *pango, GtkStyleContext *style) {
  GtkStyleContext *desc_style = gtk_style_context_new();
  gtk_style_context_set_screen(desc_style,
      gtk_style_context_get_screen(style));
  GtkWidgetPath *desc_path = gtk_widget_path_copy(
      gtk_style_context_get_path(style));
  gtk_widget_path_append_type(desc_path, G_TYPE_NONE);
  gtk_style_context_set_path(desc_style, desc_path);
  gtk_style_context_add_class(desc_style, "description");

  double desc_font_size = 16.;
  gtk_style_context_get(desc_style, GTK_STATE_FLAG_NORMAL,
      "font-size", &desc_font_size, NULL);

  g_autofree gchar *str = g_strdup_printf("%s\n<span size=\"%d\">%s</span>",
      head->name, (int) (desc_font_size * PANGO_SCALE), head->description);
  PangoLayout *layout = pango_layout_new(pango);

  pango_layout_set_markup(layout, str, -1);
  return layout;
}

static void resize(struct wd_output *output) {
  struct wd_head *head = wd_find_head(output->state, output);

  uint32_t screen_width = head->custom_mode.width;
  uint32_t screen_height = head->custom_mode.height;
  if (head->mode != NULL) {
    screen_width = head->mode->width;
    screen_height = head->mode->height;
  }
  uint32_t margin =  min(screen_width, screen_height) * SCREEN_MARGIN_PERCENT;

  GdkWindow *window = gtk_widget_get_window(output->overlay_window);
  PangoContext *pango = gtk_widget_get_pango_context(output->overlay_window);
  GtkStyleContext *style_ctx = gtk_widget_get_style_context(
      output->overlay_window);
  PangoLayout *layout = create_text_layout(head, pango, style_ctx);

  int width;
  int height;
  pango_layout_get_pixel_size(layout, &width, &height);
  g_object_unref(layout);


  GtkBorder padding;
  gtk_style_context_get_padding(style_ctx, GTK_STATE_FLAG_NORMAL, &padding);

  width = min(width, screen_width - margin * 2)
    + padding.left + padding.right;
  height = min(height, screen_height - margin * 2)
    + padding.top + padding.bottom;

  zwlr_layer_surface_v1_set_margin(output->overlay_layer_surface,
      margin, margin, margin, margin);
  zwlr_layer_surface_v1_set_size(output->overlay_layer_surface,
      width, height);

  struct wl_surface *surface = gdk_wayland_window_get_wl_surface(window);
  wl_surface_commit(surface);

  GdkDisplay *display = gdk_window_get_display(window);
  wl_display_roundtrip(gdk_wayland_display_get_wl_display(display));
}

void wd_redraw_overlay(struct wd_output *output) {
  if (output->overlay_window != NULL) {
    resize(output);
    gtk_widget_queue_draw(output->overlay_window);
  }
}

void window_realize(GtkWidget *widget, gpointer data) {
  GdkWindow *window = gtk_widget_get_window(widget);
  gdk_wayland_window_set_use_custom_surface(window);
}

void window_map(GtkWidget *widget, gpointer data) {
  struct wd_output *output = data;

  GdkWindow *window = gtk_widget_get_window(widget);
  cairo_region_t *region = cairo_region_create();
  gdk_window_input_shape_combine_region(window, region, 0, 0);
  cairo_region_destroy(region);

  struct wl_surface *surface = gdk_wayland_window_get_wl_surface(window);

  output->overlay_layer_surface = zwlr_layer_shell_v1_get_layer_surface(
      output->state->layer_shell, surface, output->wl_output,
      ZWLR_LAYER_SHELL_V1_LAYER_TOP, "output-overlay");

  zwlr_layer_surface_v1_add_listener(output->overlay_layer_surface,
      &layer_surface_listener, output);

  zwlr_layer_surface_v1_set_anchor(output->overlay_layer_surface,
      ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
      ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);

  resize(output);
}

void window_unmap(GtkWidget *widget, gpointer data) {
  struct wd_output *output = data;
  zwlr_layer_surface_v1_destroy(output->overlay_layer_surface);
}

gboolean window_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
  struct wd_output *output = data;
  struct wd_head *head = wd_find_head(output->state, output);

  GtkStyleContext *style_ctx = gtk_widget_get_style_context(widget);
  GdkRGBA fg;
  gtk_style_context_get_color(style_ctx, GTK_STATE_FLAG_NORMAL, &fg);

  int width = gtk_widget_get_allocated_width(widget);
  int height = gtk_widget_get_allocated_height(widget);
  gtk_render_background(style_ctx, cr, 0, 0, width, height);

  GtkBorder padding;
  gtk_style_context_get_padding(style_ctx, GTK_STATE_FLAG_NORMAL, &padding);
  PangoContext *pango = gtk_widget_get_pango_context(widget);
  PangoLayout *layout = create_text_layout(head, pango, style_ctx);

  gdk_cairo_set_source_rgba(cr, &fg);
  cairo_move_to(cr, padding.left, padding.top);
  pango_cairo_show_layout(cr, layout);
  g_object_unref(layout);
  return TRUE;
}

void wd_create_overlay(struct wd_output *output) {
  output->overlay_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_decorated(GTK_WINDOW(output->overlay_window), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(output->overlay_window), FALSE);
  gtk_widget_add_events(output->overlay_window, GDK_STRUCTURE_MASK);

  g_signal_connect(output->overlay_window, "realize",
      G_CALLBACK(window_realize), output);
  g_signal_connect(output->overlay_window, "map",
      G_CALLBACK(window_map), output);
  g_signal_connect(output->overlay_window, "unmap",
      G_CALLBACK(window_unmap), output);
  g_signal_connect(output->overlay_window, "draw",
      G_CALLBACK(window_draw), output);

  GtkStyleContext *style_ctx = gtk_widget_get_style_context(
      output->overlay_window);
  gtk_style_context_add_class(style_ctx, "output-overlay");
  gtk_widget_show(output->overlay_window);
}

void wd_destroy_overlay(struct wd_output *output) {
  if (output->overlay_window != NULL) {
    gtk_widget_destroy(output->overlay_window);
    output->overlay_window = NULL;
  }
}
