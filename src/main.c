/* SPDX-FileCopyrightText: 2020 Jason Francis <jason@cycles.network>
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>

#include "wdisplays.h"
#include "glviewport.h"
#include "headform.h"

__attribute__((noreturn)) void wd_fatal_error(int status, const char *message) {
  GtkWindow *parent = gtk_application_get_active_window(GTK_APPLICATION(g_application_get_default()));
  GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", message);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
  exit(status);
}

#define DEFAULT_ZOOM 0.1
#define MIN_ZOOM (1./1000.)
#define MAX_ZOOM 1000.
#define CANVAS_MARGIN 40

static const char *APP_PREFIX = "app";

static bool has_changes(const struct wd_state *state) {
  g_autoptr(GList) forms = gtk_container_get_children(GTK_CONTAINER(state->stack));
  for (GList *form_iter = forms; form_iter != NULL; form_iter = form_iter->next) {
    if (wd_head_form_has_changes(WD_HEAD_FORM(form_iter->data),
          g_object_get_data(G_OBJECT(form_iter->data), "head")))
      return true;
  }
  return false;
}

static gboolean send_apply(gpointer data) {
  struct wd_state *state = data;
  state->apply_idle = -1;
  struct wl_list *outputs = calloc(1, sizeof(*outputs));
  wl_list_init(outputs);
  g_autoptr(GList) forms = gtk_container_get_children(GTK_CONTAINER(state->stack));
  for (GList *form_iter = forms; form_iter != NULL; form_iter = form_iter->next) {
    struct wd_head_config *output = calloc(1, sizeof(*output));
    wl_list_insert(outputs, &output->link);
    output->head = g_object_get_data(G_OBJECT(form_iter->data), "head");
    wd_head_form_fill_config(WD_HEAD_FORM(form_iter->data), output);
  }
  GdkWindow *window = gtk_widget_get_window(state->stack);
  GdkDisplay *display = gdk_window_get_display(window);
  struct wl_display *wl_display = gdk_wayland_display_get_wl_display(display);
  wd_apply_state(state, outputs, wl_display);
  state->apply_pending = FALSE;
  return FALSE;
}

static void apply_state(struct wd_state *state) {
  gtk_stack_set_visible_child_name(GTK_STACK(state->header_stack), "title");
  if (!state->autoapply) {
    gtk_style_context_add_class(gtk_widget_get_style_context(state->spinner), "visible");
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(state->overlay), state->spinner, FALSE);
    gtk_spinner_start(GTK_SPINNER(state->spinner));

    gtk_widget_set_sensitive(state->stack_switcher, FALSE);
    gtk_widget_set_sensitive(state->stack, FALSE);
    gtk_widget_set_sensitive(state->zoom_in, FALSE);
    gtk_widget_set_sensitive(state->zoom_reset, FALSE);
    gtk_widget_set_sensitive(state->zoom_out, FALSE);
    gtk_widget_set_sensitive(state->menu_button, FALSE);
  }

  /* queue this once per iteration in order to prevent duplicate updates */
  if (!state->apply_pending) {
    state->apply_pending = TRUE;
    state->apply_idle = g_idle_add_full(G_PRIORITY_DEFAULT,
        send_apply, state, NULL);
  }
}

static gboolean apply_done_reset(gpointer data) {
  struct wd_state *state = data;
  state->reset_idle = -1;
  wd_ui_reset_all(state);
  return FALSE;
}

static void update_scroll_size(struct wd_state *state) {
  state->render.viewport_width = gtk_widget_get_allocated_width(state->canvas);
  state->render.viewport_height = gtk_widget_get_allocated_height(state->canvas);

  GtkAdjustment *scroll_x_adj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(state->scroller));
  GtkAdjustment *scroll_y_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(state->scroller));
  int scroll_x_upper = state->render.width;
  int scroll_y_upper = state->render.height;
  gtk_adjustment_set_upper(scroll_x_adj, MAX(0, scroll_x_upper));
  gtk_adjustment_set_upper(scroll_y_adj, MAX(0, scroll_y_upper));
  gtk_adjustment_set_page_size(scroll_x_adj, state->render.viewport_width);
  gtk_adjustment_set_page_size(scroll_y_adj, state->render.viewport_height);
  gtk_adjustment_set_page_increment(scroll_x_adj, state->render.viewport_width);
  gtk_adjustment_set_page_increment(scroll_y_adj, state->render.viewport_height);
  gtk_adjustment_set_step_increment(scroll_x_adj, state->render.viewport_width / 10);
  gtk_adjustment_set_step_increment(scroll_y_adj, state->render.viewport_height / 10);
  double x = gtk_adjustment_get_value(scroll_x_adj);
  double y = gtk_adjustment_get_value(scroll_y_adj);
  gtk_adjustment_set_value(scroll_x_adj, MIN(x, scroll_x_upper));
  gtk_adjustment_set_value(scroll_y_adj, MIN(y, scroll_y_upper));
}

/*
 * Recalculates the desired canvas size, accounting for zoom + margins.
 */
static void update_canvas_size(struct wd_state *state) {
  int xmin = 0;
  int xmax = 0;
  int ymin = 0;
  int ymax = 0;

  g_autoptr(GList) forms = gtk_container_get_children(GTK_CONTAINER(state->stack));
  for (GList *form_iter = forms; form_iter != NULL; form_iter = form_iter->next) {
    WdHeadForm *form = WD_HEAD_FORM(form_iter->data);
    if (wd_head_form_get_enabled(form)) {
      WdHeadDimensions dim;
      wd_head_form_get_dimensions(form, &dim);
      int h = dim.h;
      int w = dim.w;
      int x2 = dim.x + w;
      int y2 = dim.x + h;
      if (dim.scale > 0.) {
        w /= dim.scale;
        h /= dim.scale;
      }
      xmin = MIN(xmin, dim.x);
      xmax = MAX(xmax, x2);
      ymin = MIN(ymin, dim.y);
      ymax = MAX(ymax, y2);
    }
  }
  // update canvas sizings
  state->render.x_origin = floor(xmin * state->zoom) - CANVAS_MARGIN;
  state->render.y_origin = floor(ymin * state->zoom) - CANVAS_MARGIN;
  state->render.width = ceil((xmax - xmin) * state->zoom) + CANVAS_MARGIN * 2;
  state->render.height = ceil((ymax - ymin) * state->zoom) + CANVAS_MARGIN * 2;

  update_scroll_size(state);
}

static void cache_scroll(struct wd_state *state) {
  GtkAdjustment *scroll_x_adj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(state->scroller));
  GtkAdjustment *scroll_y_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(state->scroller));
  state->render.scroll_x = gtk_adjustment_get_value(scroll_x_adj);
  state->render.scroll_y = gtk_adjustment_get_value(scroll_y_adj);
}

static gboolean redraw_canvas(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer data);

static void update_tick_callback(struct wd_state *state) {
  bool any_animate = FALSE;
  struct wd_render_head_data *render;
  wl_list_for_each(render, &state->render.heads, link) {
    if (state->render.updated_at < render->hover_begin + HOVER_USECS
        || state->render.updated_at < render->click_begin + HOVER_USECS) {
      any_animate = TRUE;
      break;
    }
  }
  if (!any_animate && !state->capture) {
    if (state->canvas_tick != -1) {
      gtk_widget_remove_tick_callback(state->canvas, state->canvas_tick);
      state->canvas_tick = -1;
    }
  } else if (state->canvas_tick == -1) {
    state->canvas_tick =
      gtk_widget_add_tick_callback(state->canvas, redraw_canvas, state, NULL);
  }
  gtk_gl_area_queue_render(GTK_GL_AREA(state->canvas));
  gtk_gl_area_set_auto_render(GTK_GL_AREA(state->canvas), state->capture);
}

static void update_cursor(struct wd_state *state) {
  bool any_hovered = FALSE;
  struct wd_head *head;
  wl_list_for_each(head, &state->heads, link) {
    struct wd_render_head_data *render = head->render;
    if (render != NULL && render->hovered) {
      any_hovered = TRUE;
      break;
    }
  }
  GdkWindow *window = gtk_widget_get_window(state->canvas);
  if (any_hovered) {
    gdk_window_set_cursor(window, state->grab_cursor);
  } else if (state->clicked != NULL) {
    gdk_window_set_cursor(window, state->grabbing_cursor);
  } else if (state->panning) {
    gdk_window_set_cursor(window, state->move_cursor);
  } else {
    gdk_window_set_cursor(window, NULL);
  }
}

static inline void flip_anim(uint64_t *timer, uint64_t tick) {
  uint64_t animate_end = *timer + HOVER_USECS;
  if (tick < animate_end) {
    *timer = tick - (animate_end - tick);
  } else {
    *timer = tick;
  }
}

static void update_hovered(struct wd_state *state,
    gdouble mouse_x, gdouble mouse_y) {
  if (!gtk_widget_get_realized(state->canvas)) {
    return;
  }
  GdkFrameClock *clock = gtk_widget_get_frame_clock(state->canvas);
  uint64_t tick = gdk_frame_clock_get_frame_time(clock);
  bool any_hovered = FALSE;
  struct wd_render_head_data *render;
  wl_list_for_each(render, &state->render.heads, link) {
    bool init_hovered = render->hovered;
    render->hovered = FALSE;
    if (any_hovered) {
      continue;
    }
    if (state->clicked == render) {
      render->hovered = TRUE;
      any_hovered = TRUE;
    } else if (state->clicked == NULL) {
      if (mouse_x >= render->x1 && mouse_x < render->x2 &&
          mouse_y >= render->y1 && mouse_y < render->y2) {
        render->hovered = TRUE;
        any_hovered = TRUE;
      }
    }
    if (init_hovered != render->hovered) {
      flip_anim(&render->hover_begin, tick);
    }
  }
  update_cursor(state);
  update_tick_callback(state);
}

static inline void color_to_float_array(GtkStyleContext *ctx,
    const char *color_name, float out[4]) {
  GdkRGBA color;
  gtk_style_context_lookup_color(ctx, color_name, &color);
  out[0] = color.red;
  out[1] = color.green;
  out[2] = color.blue;
  out[3] = color.alpha;
}

#define SWAP(_type, _a, _b) { _type _tmp = (_a); (_a) = (_b); (_b) = _tmp; }

static void queue_canvas_draw(struct wd_state *state) {
  GtkStyleContext *style_ctx = gtk_widget_get_style_context(state->canvas);
  color_to_float_array(style_ctx,
      "theme_fg_color", state->render.fg_color);
  color_to_float_array(style_ctx,
      "theme_bg_color", state->render.bg_color);
  color_to_float_array(style_ctx,
      "borders", state->render.border_color);
  color_to_float_array(style_ctx,
      "theme_selected_bg_color", state->render.selection_color);

  cache_scroll(state);

  g_autoptr(GList) forms = gtk_container_get_children(GTK_CONTAINER(state->stack));
  for (GList *form_iter = forms; form_iter != NULL; form_iter = form_iter->next) {
    WdHeadForm *form = WD_HEAD_FORM(form_iter->data);
    if (wd_head_form_get_enabled(form)) {
      WdHeadDimensions dim;
      wd_head_form_get_dimensions(form, &dim);
      double w = dim.w;
      double h = dim.h;
      double scale = dim.scale;
      if (scale <= 0.)
        scale = 1.;

      struct wd_head *head = g_object_get_data(G_OBJECT(form_iter->data), "head");
      if (head->render == NULL) {
        head->render = calloc(1, sizeof(*head->render));
        wl_list_insert(&state->render.heads, &head->render->link);
      }
      struct wd_render_head_data *render = head->render;
      render->queued.rotation = dim.rotation_id;
      if (render->queued.rotation & 1) {
        SWAP(int, w, h);
      }
      render->queued.x_invert = dim.flipped;
      render->x1 = floor(dim.x * state->zoom - state->render.scroll_x - state->render.x_origin);
      render->y1 = floor(dim.y * state->zoom - state->render.scroll_y - state->render.y_origin);
      render->x2 = floor(render->x1 + w * state->zoom / scale);
      render->y2 = floor(render->y1 + h * state->zoom / scale);
    }
  }
  gtk_gl_area_queue_render(GTK_GL_AREA(state->canvas));
}

static void show_apply(struct wd_state *state) {
  const gchar *page = "title";
  if (has_changes(state)) {
    if (state->autoapply) {
      apply_state(state);
    } else {
      page = "apply";
    }
  }
  gtk_stack_set_visible_child_name(GTK_STACK(state->header_stack), page);
}

static void update_ui(WdHeadForm *form, enum wd_head_fields fields,
    gpointer data) {
  struct wd_state *state = data;
  show_apply(state);
  update_canvas_size(state);
  queue_canvas_draw(state);
}

void wd_ui_reset_heads(struct wd_state *state) {
  if (state->stack == NULL) {
    return;
  }

  g_autoptr(GList) forms = gtk_container_get_children(GTK_CONTAINER(state->stack));
  GList *form_iter = forms;
  struct wd_head *head;
  int i = 0;
  wl_list_for_each(head, &state->heads, link) {
    if (form_iter == NULL) {
      GtkWidget *form = wd_head_form_new();;
      g_object_set_data(G_OBJECT(form), "head", head);
      g_signal_connect(form, "changed", G_CALLBACK(update_ui), state);
      g_autofree gchar *page_name = g_strdup_printf("%d", i);
      gtk_stack_add_titled(GTK_STACK(state->stack), form, page_name, head->name);
      wd_head_form_update(WD_HEAD_FORM(form), head, WD_FIELDS_ALL);
    } else {
      GtkWidget *form = GTK_WIDGET(form_iter->data);
      if (head != g_object_get_data(G_OBJECT(form), "head")) {
        g_object_set_data(G_OBJECT(form), "head", head);
        gtk_container_child_set(GTK_CONTAINER(state->stack), form, "title", head->name, NULL);
        wd_head_form_update(WD_HEAD_FORM(form), head, WD_FIELDS_ALL);
      }
      form_iter = form_iter->next;
    }
    i++;
  }
  // remove everything else
  for (; form_iter != NULL; form_iter = form_iter->next) {
    gtk_container_remove(GTK_CONTAINER(state->stack), GTK_WIDGET(form_iter->data));
  }
  update_canvas_size(state);
  queue_canvas_draw(state);
}

void wd_ui_reset_head(const struct wd_head *head, enum wd_head_fields fields) {
  if (head->state->stack == NULL) {
    return;
  }
  g_autoptr(GList) forms = gtk_container_get_children(GTK_CONTAINER(head->state->stack));
  for (GList *form_iter = forms; form_iter != NULL; form_iter = form_iter->next) {
    GtkWidget *form = GTK_WIDGET(form_iter->data);
    const struct wd_head *other = g_object_get_data(G_OBJECT(form), "head");
    if (head == other) {
      if (fields & WD_FIELD_NAME)
        gtk_container_child_set(GTK_CONTAINER(head->state->stack), form, "title", head->name, NULL);
      wd_head_form_update(WD_HEAD_FORM(form), head, fields);
      break;
    }
  }
  update_canvas_size(head->state);
  queue_canvas_draw(head->state);
}

void wd_ui_reset_all(struct wd_state *state) {
  wd_ui_reset_heads(state);
  g_autoptr(GList) forms = gtk_container_get_children(GTK_CONTAINER(state->stack));
  for (GList *form_iter = forms; form_iter != NULL; form_iter = form_iter->next) {
    GtkWidget *form = GTK_WIDGET(form_iter->data);
    struct wd_head *head = g_object_get_data(G_OBJECT(form), "head");
    gtk_container_child_set(GTK_CONTAINER(state->stack), form, "title", head->name, NULL);
    wd_head_form_update(WD_HEAD_FORM(form_iter->data), head, WD_FIELDS_ALL);
  }
  update_canvas_size(state);
  queue_canvas_draw(state);
}

void wd_ui_apply_done(struct wd_state *state, struct wl_list *outputs) {
  gtk_style_context_remove_class(gtk_widget_get_style_context(state->spinner), "visible");
  gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(state->overlay), state->spinner, TRUE);
  gtk_spinner_stop(GTK_SPINNER(state->spinner));

  gtk_widget_set_sensitive(state->stack_switcher, TRUE);
  gtk_widget_set_sensitive(state->stack, TRUE);
  gtk_widget_set_sensitive(state->zoom_in, TRUE);
  gtk_widget_set_sensitive(state->zoom_reset, TRUE);
  gtk_widget_set_sensitive(state->zoom_out, TRUE);
  gtk_widget_set_sensitive(state->menu_button, TRUE);
  if (!state->autoapply) {
    show_apply(state);
  }
  state->reset_idle = g_idle_add_full(G_PRIORITY_DEFAULT,
      apply_done_reset, state, NULL);
}

void wd_ui_show_error(struct wd_state *state, const char *message) {
  gtk_label_set_text(GTK_LABEL(state->info_label), message);
  gtk_widget_show(state->info_bar);
  gtk_info_bar_set_revealed(GTK_INFO_BAR(state->info_bar), TRUE);
}

// BEGIN GLOBAL CALLBACKS
static void cleanup(GtkWidget *window, gpointer data) {
  struct wd_state *state = data;
  if (state->reset_idle != -1)
    g_source_remove(state->reset_idle);
  if (state->apply_idle != -1)
    g_source_remove(state->apply_idle);
  g_object_unref(state->grab_cursor);
  g_object_unref(state->grabbing_cursor);
  g_object_unref(state->move_cursor);
  wd_state_destroy(state);
}

static void monitor_added(GdkDisplay *display, GdkMonitor *monitor, gpointer data) {
  struct wl_display *wl_display = gdk_wayland_display_get_wl_display(display);
  wd_add_output(data, gdk_wayland_monitor_get_wl_output(monitor), wl_display);
}

static void monitor_removed(GdkDisplay *display, GdkMonitor *monitor, gpointer data) {
  struct wl_display *wl_display = gdk_wayland_display_get_wl_display(display);
  wd_remove_output(data, gdk_wayland_monitor_get_wl_output(monitor), wl_display);
}

static void canvas_realize(GtkWidget *widget, gpointer data) {
  gtk_gl_area_make_current(GTK_GL_AREA(widget));
  if (gtk_gl_area_get_error(GTK_GL_AREA(widget)) != NULL) {
    return;
  }

  struct wd_state *state = data;
  state->gl_data = wd_gl_setup();
}

static inline bool size_changed(const struct wd_render_head_data *render) {
  return render->x2 - render->x1 != render->tex_width ||
    render->y2 - render->y1 != render->tex_height;
}

static inline void cairo_set_source_color(cairo_t *cr, float color[4]) {
  cairo_set_source_rgba(cr, color[0], color[1], color[2], color[3]);
}

static void update_zoom(struct wd_state *state) {
  g_autofree gchar *zoom_percent = g_strdup_printf("%.f%%", state->zoom * 100.);
  gtk_button_set_label(GTK_BUTTON(state->zoom_reset), zoom_percent);
  gtk_widget_set_sensitive(state->zoom_in, state->zoom < MAX_ZOOM);
  gtk_widget_set_sensitive(state->zoom_out, state->zoom > MIN_ZOOM);

  update_canvas_size(state);
  queue_canvas_draw(state);
}

static void zoom_to(struct wd_state *state, double zoom) {
  state->zoom = zoom;
  state->zoom = MAX(state->zoom, MIN_ZOOM);
  state->zoom = MIN(state->zoom, MAX_ZOOM);
  update_zoom(state);
}

static void zoom_out(GSimpleAction *action, GVariant *param, gpointer data) {
  struct wd_state *state = data;
  zoom_to(state, state->zoom * 0.75);
}

static void zoom_reset(GSimpleAction *action, GVariant *param, gpointer data) {
  struct wd_state *state = data;
  zoom_to(state, DEFAULT_ZOOM);
}

static void zoom_in(GSimpleAction *action, GVariant *param, gpointer data) {
  struct wd_state *state = data;
  zoom_to(state, state->zoom / 0.75);
}

#define TEXT_MARGIN 5

static cairo_surface_t *draw_head(PangoContext *pango,
    struct wd_render_data *info, const char *name,
    unsigned width, unsigned height) {
  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
      width, height);
  cairo_t *cr = cairo_create(surface);

  cairo_rectangle(cr, 0., 0., width, height);
  cairo_set_source_color(cr, info->border_color);
  cairo_fill(cr);

  PangoLayout *layout = pango_layout_new(pango);
  pango_layout_set_text(layout, name, -1);
  int text_width = pango_units_from_double(width - TEXT_MARGIN * 2);
  int text_height = pango_units_from_double(height - TEXT_MARGIN * 2);
  pango_layout_set_width(layout, MAX(text_width, 0));
  pango_layout_set_height(layout, MAX(text_height, 0));
  pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
  pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
  pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);

  cairo_set_source_color(cr, info->fg_color);
  pango_layout_get_size(layout, &text_width, &text_height);
  cairo_move_to(cr, TEXT_MARGIN, (height - PANGO_PIXELS(text_height)) / 2);
  pango_cairo_show_layout(cr, layout);
  g_object_unref(layout);

  cairo_destroy(cr);
  cairo_surface_flush(surface);
  return surface;
}

static void canvas_render(GtkGLArea *area, GdkGLContext *context, gpointer data) {
  struct wd_state *state = data;

  PangoContext *pango = gtk_widget_get_pango_context(state->canvas);
  GdkFrameClock *clock = gtk_widget_get_frame_clock(state->canvas);
  uint64_t tick = gdk_frame_clock_get_frame_time(clock);

  wd_capture_frame(state);

  struct wd_head *head;
  wl_list_for_each(head, &state->heads, link) {
    struct wd_render_head_data *render = head->render;
    struct wd_output *output = wd_find_output(state, head);
    struct wd_frame *frame = NULL;
    if (output != NULL && !wl_list_empty(&output->frames)) {
      frame = wl_container_of(output->frames.prev, frame, link);
    }
    if (render != NULL) {
      if (state->capture && frame != NULL && frame->pixels != NULL) {
        if (frame->tick > render->updated_at) {
          render->tex_stride = frame->stride;
          render->tex_width = frame->width;
          render->tex_height = frame->height;
          render->pixels = frame->pixels;
          render->preview = TRUE;
          render->updated_at = tick;
          render->y_invert = frame->y_invert;
          render->swap_rgb = frame->swap_rgb;
        }
        if (render->preview) {
          render->active.rotation = render->queued.rotation;
          render->active.x_invert = render->queued.x_invert;
        }
      } else if (render->preview
          || render->pixels == NULL || size_changed(render)) {
        render->tex_width = render->x2 - render->x1;
        render->tex_height = render->y2 - render->y1;
        render->preview = FALSE;
        if (head->surface != NULL) {
          cairo_surface_destroy(head->surface);
        }
        head->surface = draw_head(pango, &state->render, head->name,
            render->tex_width, render->tex_height);
        render->pixels = cairo_image_surface_get_data(head->surface);
        render->tex_stride = cairo_image_surface_get_stride(head->surface);
        render->updated_at = tick;
        render->active.rotation = 0;
        render->active.x_invert = FALSE;
        render->y_invert = FALSE;
        render->swap_rgb = FALSE;
      }
    }
  }

  wd_gl_render(state->gl_data, &state->render, tick);
  state->render.updated_at = tick;
}

static void canvas_unrealize(GtkWidget *widget, gpointer data) {
  gtk_gl_area_make_current(GTK_GL_AREA(widget));
  if (gtk_gl_area_get_error(GTK_GL_AREA(widget)) != NULL) {
    return;
  }
  struct wd_state *state = data;

  GdkDisplay *gdk_display = gdk_display_get_default();
  struct wl_display *display = gdk_wayland_display_get_wl_display(gdk_display);
  wd_capture_wait(state, display);

  wd_gl_cleanup(state->gl_data);
  state->gl_data = NULL;
}

static void set_clicked_head(struct wd_state *state,
    struct wd_render_head_data *clicked) {
  GdkFrameClock *clock = gtk_widget_get_frame_clock(state->canvas);
  uint64_t tick = gdk_frame_clock_get_frame_time(clock);
  if (clicked != state->clicked) {
    if (state->clicked != NULL) {
      state->clicked->clicked = FALSE;
      flip_anim(&state->clicked->click_begin, tick);
    }
    if (clicked != NULL) {
      clicked->clicked = TRUE;
      flip_anim(&clicked->click_begin, tick);
    }
    update_tick_callback(state);
  }
  state->clicked = clicked;
}

static void canvas_drag1_begin(GtkGestureDrag *drag,
    gdouble mouse_x, gdouble mouse_y, gpointer data) {
  struct wd_state *state = data;

  struct wd_render_head_data *render;
  state->clicked = NULL;
  wl_list_for_each(render, &state->render.heads, link) {
    if (mouse_x >= render->x1 && mouse_x < render->x2 &&
        mouse_y >= render->y1 && mouse_y < render->y2) {
      set_clicked_head(state, render);
      state->drag_start.x = mouse_x;
      state->drag_start.y = mouse_y;
      state->head_drag_start.x = (mouse_x - render->x1) / (render->x2 - render->x1);
      state->head_drag_start.y = (mouse_y - render->y1) / (render->y2 - render->y1);
      break;
    }
  }
  if (state->clicked != NULL) {
    wl_list_remove(&state->clicked->link);
    wl_list_insert(&state->render.heads, &state->clicked->link);

    struct wd_render_head_data *render;
    wl_list_for_each(render, &state->render.heads, link) {
      render->updated_at = 0;
      render->preview = TRUE;
    }
    gtk_gl_area_queue_render(GTK_GL_AREA(state->canvas));
    g_autoptr(GList) forms = gtk_container_get_children(GTK_CONTAINER(state->stack));
    for (GList *form_iter = forms; form_iter != NULL; form_iter = form_iter->next) {
      const struct wd_head *other = g_object_get_data(G_OBJECT(form_iter->data), "head");
      if (state->clicked == other->render) {
        gtk_stack_set_visible_child(GTK_STACK(state->stack), form_iter->data);
        break;
      }
    }
  }
}

#define SNAP_DIST 6.

static void canvas_drag1_update(GtkGestureDrag *drag,
    gdouble delta_x, gdouble delta_y, gpointer data) {
  struct wd_state *state = data;

  if (state->clicked == NULL)
    return;
  WdHeadForm *form = NULL;
  g_autoptr(GList) forms = gtk_container_get_children(GTK_CONTAINER(state->stack));
  for (GList *form_iter = forms; form_iter != NULL; form_iter = form_iter->next) {
    const struct wd_head *other = g_object_get_data(G_OBJECT(form_iter->data), "head");
    if (state->clicked == other->render) {
      form = WD_HEAD_FORM(form_iter->data);
      break;
    }
  }
  if (!form)
    return;
  WdHeadDimensions dim;
  wd_head_form_get_dimensions(form, &dim);
  struct wd_point size = { .x = dim.w, .y = dim.h };
  if (dim.scale > 0.) {
    size.x /= dim.scale;
    size.y /= dim.scale;
  }
  if (dim.rotation_id & 1) {
    SWAP(int, size.x, size.y);
  }
  struct wd_point tl = { /* top left */
    .x = (state->drag_start.x + delta_x - state->head_drag_start.x * size.x * state->zoom
        + state->render.x_origin + state->render.scroll_x) / state->zoom,
    .y = (state->drag_start.y + delta_y - state->head_drag_start.y * size.y * state->zoom
        + state->render.y_origin + state->render.scroll_y) / state->zoom
  };

  const struct wd_point br = { /* bottom right */
    .x = tl.x + size.x,
    .y = tl.y + size.y
  };
  struct wd_point new_pos = tl;
  float snap = SNAP_DIST / state->zoom;

  GdkEvent *event = gtk_get_current_event();
  GdkModifierType mod_state = event->motion.state;

  /* snapping */
  for (GList *form_iter = forms; form_iter != NULL; form_iter = form_iter->next) {
    WdHeadForm *other_form = WD_HEAD_FORM(form_iter->data);
    const struct wd_head *other = g_object_get_data(G_OBJECT(other_form), "head");
    if (other->render != state->clicked && !(mod_state & GDK_SHIFT_MASK)) {
      WdHeadDimensions other_dim;
      wd_head_form_get_dimensions(other_form, &other_dim);
      double x1 = other_dim.x;
      double y1 = other_dim.y;
      double w = other_dim.w;
      double h = other_dim.h;
      if (other_dim.scale > 0.) {
        w /= other_dim.scale;
        h /= other_dim.scale;
      }
      if (other_dim.rotation_id & 1) {
        SWAP(int, w, h);
      }
      double x2 = x1 + w;
      double y2 = y1 + h;
      if (fabs(br.x) <= snap)
        new_pos.x = -size.x;
      if (fabs(br.y) <= snap)
        new_pos.y = -size.y;
      if (fabs(br.x - x1) <= snap)
        new_pos.x = x1 - size.x;
      if (fabs(br.x - x2) <= snap)
        new_pos.x = x2 - size.x;
      if (fabs(br.y - y1) <= snap)
        new_pos.y = y1 - size.y;
      if (fabs(br.y - y2) <= snap)
        new_pos.y = y2 - size.y;

      if (fabs(tl.x) <= snap)
        new_pos.x = 0.;
      if (fabs(tl.y) <= snap)
        new_pos.y = 0.;
      if (fabs(tl.x - x1) <= snap)
        new_pos.x = x1;
      if (fabs(tl.x - x2) <= snap)
        new_pos.x = x2;
      if (fabs(tl.y - y1) <= snap)
        new_pos.y = y1;
      if (fabs(tl.y - y2) <= snap)
        new_pos.y = y2;
    }
  }
  wd_head_form_set_position(form, new_pos.x, new_pos.y);
}

static void canvas_drag1_end(GtkGestureDrag *drag,
    gdouble mouse_x, gdouble mouse_y, gpointer data) {
  struct wd_state *state = data;
  set_clicked_head(state, NULL);
  update_cursor(state);
}

static void canvas_drag2_begin(GtkGestureDrag *drag,
    gdouble mouse_x, gdouble mouse_y, gpointer data) {
  struct wd_state *state = data;
  state->panning = TRUE;
  GtkAdjustment *xadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(state->scroller));
  GtkAdjustment *yadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(state->scroller));
  state->pan_start.x = gtk_adjustment_get_value(xadj);
  state->pan_start.y = gtk_adjustment_get_value(yadj);
}

static void canvas_drag2_update(GtkGestureDrag *drag,
    gdouble delta_x, gdouble delta_y, gpointer data) {
  struct wd_state *state = data;
  GtkAdjustment *xadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(state->scroller));
  GtkAdjustment *yadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(state->scroller));
  gtk_adjustment_set_value(xadj, state->pan_start.x  + delta_x);
  gtk_adjustment_set_value(yadj, state->pan_start.y  + delta_y);
  queue_canvas_draw(state);
}

static void canvas_drag2_end(GtkGestureDrag *drag,
    gdouble mouse_x, gdouble mouse_y, gpointer data) {
  struct wd_state *state = data;
  state->panning = FALSE;
  update_cursor(state);
}

static void canvas_motion(GtkEventControllerMotion *controller,
      gdouble mouse_x, gdouble mouse_y, gpointer data) {
  struct wd_state *state = data;
  update_hovered(state, mouse_x, mouse_y);
}

static void canvas_enter(GtkEventControllerMotion *controller,
      gdouble x, gdouble y, gpointer data) {
  struct wd_state *state = data;
  GdkEvent *event = gtk_get_current_event();
  GdkModifierType mod_state = event->crossing.state;

  if (!(mod_state & GDK_BUTTON1_MASK)) {
    set_clicked_head(state, NULL);
  }
  if (!(mod_state & GDK_BUTTON2_MASK)) {
    state->panning = FALSE;
  }
  update_cursor(state);
}

static void canvas_leave(GtkEventControllerMotion *controller,
      gpointer data) {
  struct wd_state *state = data;
  struct wd_render_head_data *render;
  wl_list_for_each(render, &state->render.heads, link) {
    render->hovered = FALSE;
  }
  update_tick_callback(state);
}

static gboolean canvas_scroll(GtkEventControllerScroll *controller,
    gdouble delta_x, gdouble delta_y, gpointer data) {
  struct wd_state *state = data;
  GdkEvent *event = gtk_get_current_event();
  GdkModifierType mod_state = event->scroll.state;

  if (mod_state & GDK_CONTROL_MASK) {
    if (delta_y)
      zoom_to(state, state->zoom * pow(0.75, delta_y));
  } else {
    GtkAdjustment *xadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(state->scroller));
    GtkAdjustment *yadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(state->scroller));
    double xstep = gtk_adjustment_get_step_increment(xadj);
    double ystep = gtk_adjustment_get_step_increment(yadj);
    if (delta_x)
      gtk_adjustment_set_value(xadj, gtk_adjustment_get_value(xadj) + xstep * delta_x);
    if (delta_y)
      gtk_adjustment_set_value(yadj, gtk_adjustment_get_value(yadj) + ystep * delta_y);
  }
  return TRUE;
}

static void canvas_resize(GtkWidget *widget, GdkRectangle *allocation,
    gpointer data) {
  struct wd_state *state = data;
  update_scroll_size(state);
}

static void cancel_changes(GSimpleAction *action, GVariant *param, gpointer data) {
  struct wd_state *state = data;
  gtk_stack_set_visible_child_name(GTK_STACK(state->header_stack), "title");
  wd_ui_reset_all(state);
}

static void apply_changes(GSimpleAction *action, GVariant *param, gpointer data) {
  apply_state(data);
}

static void info_response(GtkInfoBar *info_bar, gint response_id, gpointer data) {
  gtk_info_bar_set_revealed(info_bar, FALSE);
}

static void info_bar_animation_done(GObject *object, GParamSpec *pspec, gpointer data) {
  gboolean done = gtk_revealer_get_child_revealed(GTK_REVEALER(object));
  if (!done) {
    struct wd_state *state = data;
    gtk_widget_set_visible(state->info_bar, gtk_revealer_get_reveal_child(GTK_REVEALER(object)));
  }
}

static void auto_apply_selected(GSimpleAction *action, GVariant *param, gpointer data) {
  struct wd_state *state = data;
  state->autoapply = g_variant_get_boolean(param);
  g_simple_action_set_state(action, param);
}

static gboolean redraw_canvas(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer data) {
  struct wd_state *state = data;
  if (state->capture) {
    wd_capture_frame(state);
  }
  update_tick_callback(state);
  queue_canvas_draw(state);
  return G_SOURCE_CONTINUE;
}

static void capture_selected(GSimpleAction *action, GVariant *param, gpointer data) {
  struct wd_state *state = data;
  state->capture = g_variant_get_boolean(param);
  g_simple_action_set_state(action, param);
  update_tick_callback(state);
}

static void overlay_selected(GSimpleAction *action, GVariant *param, gpointer data) {
  struct wd_state *state = data;
  state->show_overlay = g_variant_get_boolean(param);
  g_simple_action_set_state(action, param);

  struct wd_output *output;
  wl_list_for_each(output, &state->outputs, link) {
    if (state->show_overlay) {
      wd_create_overlay(output);
    } else {
      wd_destroy_overlay(output);
    }
  }
}

static void window_state_changed(GtkWidget *window, GdkEventWindowState *event,
    gpointer data) {
  struct wd_state *state = data;
  if (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) {
    g_object_ref(state->header_stack);
    GtkWidget *container = gtk_widget_get_parent(state->header_stack);
    gtk_container_remove(GTK_CONTAINER(container), state->header_stack);
    if (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) {
      gtk_container_add(GTK_CONTAINER(state->main_box), state->header_stack);
      gtk_box_reorder_child(GTK_BOX(state->main_box), state->header_stack, 0);
    } else {
      gtk_widget_unrealize(window);
      gtk_window_set_titlebar(GTK_WINDOW(window), state->header_stack);
      gtk_widget_map(window);
    }
    g_object_unref(state->header_stack);
  }
}

static void activate(GtkApplication* app, gpointer user_data) {
  GdkDisplay *gdk_display = gdk_display_get_default();
  if (!GDK_IS_WAYLAND_DISPLAY(gdk_display)) {
    wd_fatal_error(1, "This program is only usable on Wayland sessions.");
  }

  struct wd_state *state = wd_state_create();
  state->zoom = DEFAULT_ZOOM;
  state->canvas_tick = -1;
  state->apply_idle = -1;
  state->reset_idle = -1;

  GtkCssProvider *css_provider = gtk_css_provider_new();
  gtk_css_provider_load_from_resource(css_provider,
      WDISPLAYS_RESOURCE_PREFIX "/style.css");
  gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css_provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  state->grab_cursor = gdk_cursor_new_from_name(gdk_display, "grab");
  state->grabbing_cursor = gdk_cursor_new_from_name(gdk_display, "grabbing");
  state->move_cursor = gdk_cursor_new_from_name(gdk_display, "move");

  GtkBuilder *builder = gtk_builder_new_from_resource(
      WDISPLAYS_RESOURCE_PREFIX "/wdisplays.ui");
  GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "heads_window"));
  state->main_box = GTK_WIDGET(gtk_builder_get_object(builder, "main_box"));
  state->header_stack = GTK_WIDGET(gtk_builder_get_object(builder, "header_stack"));
  state->stack_switcher = GTK_WIDGET(gtk_builder_get_object(builder, "heads_stack_switcher"));
  state->stack = GTK_WIDGET(gtk_builder_get_object(builder, "heads_stack"));
  state->scroller = GTK_WIDGET(gtk_builder_get_object(builder, "heads_scroll"));
  state->spinner = GTK_WIDGET(gtk_builder_get_object(builder, "spinner"));
  state->zoom_out = GTK_WIDGET(gtk_builder_get_object(builder, "zoom_out"));
  state->zoom_reset = GTK_WIDGET(gtk_builder_get_object(builder, "zoom_reset"));
  state->zoom_in = GTK_WIDGET(gtk_builder_get_object(builder, "zoom_in"));
  state->overlay = GTK_WIDGET(gtk_builder_get_object(builder, "overlay"));
  state->info_bar = GTK_WIDGET(gtk_builder_get_object(builder, "heads_info"));
  state->info_label = GTK_WIDGET(gtk_builder_get_object(builder, "heads_info_label"));
  state->menu_button = GTK_WIDGET(gtk_builder_get_object(builder, "menu_button"));

  g_signal_connect(window, "window-state-event", G_CALLBACK(window_state_changed), state);
  g_signal_connect(window, "destroy", G_CALLBACK(cleanup), state);

  state->canvas = wd_gl_viewport_new();
  gtk_widget_add_events(state->canvas, GDK_POINTER_MOTION_MASK
      | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK
      | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
  g_signal_connect(state->canvas, "realize", G_CALLBACK(canvas_realize), state);
  g_signal_connect(state->canvas, "render", G_CALLBACK(canvas_render), state);
  g_signal_connect(state->canvas, "unrealize", G_CALLBACK(canvas_unrealize), state);
  g_signal_connect(state->canvas, "size-allocate", G_CALLBACK(canvas_resize), state);
  gtk_gl_area_set_required_version(GTK_GL_AREA(state->canvas), 2, 0);
  gtk_gl_area_set_use_es(GTK_GL_AREA(state->canvas), TRUE);
  gtk_gl_area_set_has_alpha(GTK_GL_AREA(state->canvas), TRUE);
  gtk_gl_area_set_auto_render(GTK_GL_AREA(state->canvas), state->capture);

  GtkGesture *canvas_drag1_controller = gtk_gesture_drag_new(state->canvas);
  GtkGesture *canvas_drag2_controller = gtk_gesture_drag_new(state->canvas);
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(canvas_drag2_controller), 2);
  GtkEventController *canvas_motion_controller = gtk_event_controller_motion_new(state->canvas);
  gtk_event_controller_set_propagation_phase(canvas_motion_controller, GTK_PHASE_CAPTURE);
  GtkEventController *canvas_scroll_controller = gtk_event_controller_scroll_new(state->canvas, GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  gtk_event_controller_set_propagation_phase(canvas_scroll_controller, GTK_PHASE_CAPTURE);
  g_signal_connect(canvas_drag1_controller, "drag-begin", G_CALLBACK(canvas_drag1_begin), state);
  g_signal_connect(canvas_drag1_controller, "drag-update", G_CALLBACK(canvas_drag1_update), state);
  g_signal_connect(canvas_drag1_controller, "drag-end", G_CALLBACK(canvas_drag1_end), state);
  g_signal_connect(canvas_drag2_controller, "drag-begin", G_CALLBACK(canvas_drag2_begin), state);
  g_signal_connect(canvas_drag2_controller, "drag-update", G_CALLBACK(canvas_drag2_update), state);
  g_signal_connect(canvas_drag2_controller, "drag-end", G_CALLBACK(canvas_drag2_end), state);
  g_signal_connect(canvas_motion_controller, "enter", G_CALLBACK(canvas_enter), state);
  g_signal_connect(canvas_motion_controller, "leave", G_CALLBACK(canvas_leave), state);
  g_signal_connect(canvas_motion_controller, "motion", G_CALLBACK(canvas_motion), state);
  g_signal_connect(canvas_scroll_controller, "scroll", G_CALLBACK(canvas_scroll), state);

  gtk_container_add(GTK_CONTAINER(state->scroller), state->canvas);

  GtkAdjustment *scroll_x_adj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(state->scroller));
  GtkAdjustment *scroll_y_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(state->scroller));
  g_signal_connect_swapped(scroll_x_adj, "value-changed", G_CALLBACK(queue_canvas_draw), state);
  g_signal_connect_swapped(scroll_y_adj, "value-changed", G_CALLBACK(queue_canvas_draw), state);

  update_zoom(state);

  GSimpleActionGroup *main_actions = g_simple_action_group_new();
  gtk_widget_insert_action_group(window, APP_PREFIX, G_ACTION_GROUP(main_actions));
  g_object_unref(main_actions);

  GSimpleAction *action = g_simple_action_new("apply-changes", NULL);
  g_signal_connect(action, "activate", G_CALLBACK(apply_changes), state);
  g_action_map_add_action(G_ACTION_MAP(main_actions), G_ACTION(action));

  action = g_simple_action_new("cancel-changes", NULL);
  g_signal_connect(action, "activate", G_CALLBACK(cancel_changes), state);
  g_action_map_add_action(G_ACTION_MAP(main_actions), G_ACTION(action));

  action = g_simple_action_new("zoom-out", NULL);
  g_signal_connect(action, "activate", G_CALLBACK(zoom_out), state);
  g_action_map_add_action(G_ACTION_MAP(main_actions), G_ACTION(action));

  action = g_simple_action_new("zoom-reset", NULL);
  g_signal_connect(action, "activate", G_CALLBACK(zoom_reset), state);
  g_action_map_add_action(G_ACTION_MAP(main_actions), G_ACTION(action));

  action = g_simple_action_new("zoom-in", NULL);
  g_signal_connect(action, "activate", G_CALLBACK(zoom_in), state);
  g_action_map_add_action(G_ACTION_MAP(main_actions), G_ACTION(action));

  action = g_simple_action_new_stateful("auto-apply", NULL,
      g_variant_new_boolean(state->autoapply));
  g_signal_connect(action, "change-state", G_CALLBACK(auto_apply_selected), state);
  g_action_map_add_action(G_ACTION_MAP(main_actions), G_ACTION(action));

  GSimpleAction *capture_action = g_simple_action_new_stateful("capture-screens", NULL,
      g_variant_new_boolean(state->capture));
  g_signal_connect(capture_action, "change-state", G_CALLBACK(capture_selected), state);
  g_action_map_add_action(G_ACTION_MAP(main_actions), G_ACTION(capture_action));

  GSimpleAction *overlay_action = g_simple_action_new_stateful("show-overlay", NULL,
      g_variant_new_boolean(state->show_overlay));
  g_signal_connect(overlay_action, "change-state", G_CALLBACK(overlay_selected), state);
  g_action_map_add_action(G_ACTION_MAP(main_actions), G_ACTION(overlay_action));

  GMenu *main_menu = g_menu_new();
  g_menu_append(main_menu, "_Automatically Apply Changes", "app.auto-apply");
  g_menu_append(main_menu, "_Show Screen Contents", "app.capture-screens");
  g_menu_append(main_menu, "_Overlay Screen Names", "app.show-overlay");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(state->menu_button), G_MENU_MODEL(main_menu));

  g_signal_connect(state->info_bar, "response", G_CALLBACK(info_response), state);
  /* first child of GtkInfoBar is always GtkRevealer */
  g_autoptr(GList) info_children = gtk_container_get_children(GTK_CONTAINER(state->info_bar));
  g_signal_connect(info_children->data, "notify::child-revealed", G_CALLBACK(info_bar_animation_done), state);

  struct wl_display *display = gdk_wayland_display_get_wl_display(gdk_display);
  wd_add_output_management_listener(state, display);

  if (state->output_manager == NULL) {
    wd_fatal_error(1, "Compositor doesn't support wlr-output-management-unstable-v1");
  }
  if (state->xdg_output_manager == NULL) {
    wd_fatal_error(1, "Compositor doesn't support xdg-output-unstable-v1");
  }
  if (state->copy_manager == NULL) {
    state->capture = FALSE;
    g_simple_action_set_state(capture_action, g_variant_new_boolean(state->capture));
    g_simple_action_set_enabled(capture_action, FALSE);
  }
  if (state->layer_shell == NULL) {
    state->show_overlay = FALSE;
    g_simple_action_set_state(overlay_action, g_variant_new_boolean(state->show_overlay));
    g_simple_action_set_enabled(overlay_action, FALSE);
  }

  int n_monitors = gdk_display_get_n_monitors(gdk_display);
  for (int i = 0; i < n_monitors; i++) {
    GdkMonitor *monitor = gdk_display_get_monitor(gdk_display, i);
    wd_add_output(state, gdk_wayland_monitor_get_wl_output(monitor), display);
  }

  g_signal_connect(gdk_display, "monitor-added", G_CALLBACK(monitor_added), state);
  g_signal_connect(gdk_display, "monitor-removed", G_CALLBACK(monitor_removed), state);

  gtk_application_add_window(app, GTK_WINDOW(window));
  gtk_widget_show_all(window);
  g_object_unref(builder);
  update_tick_callback(state);
}
// END GLOBAL CALLBACKS

int main(int argc, char *argv[]) {
  g_setenv("GDK_GL", "gles", FALSE);
  GtkApplication *app = gtk_application_new(WDISPLAYS_APP_ID, G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);

  return status;
}
