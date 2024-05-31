/* SPDX-FileCopyrightText: 2019 Jason Francis <jason@cycles.network>
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* SPDX-FileCopyrightText: 2017-2019 Simon Ser
 * SPDX-License-Identifier: MIT */

/*
 * Parts of this file are taken from emersion/kanshi:
 * https://github.com/emersion/kanshi/blob/38d27474b686fcc8324cc5e454741a49577c0988/main.c
 */

#define _GNU_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "wdisplays.h"

#include "wlr-output-management-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

extern int store_config(struct wl_list *outputs);

static void noop() {
  // This space is intentionally left blank
}

struct wd_pending_config {
  struct wd_state *state;
  struct wl_list *outputs;
};

static void destroy_pending(struct wd_pending_config *pending) {
  struct wd_head_config *output, *tmp;
  wl_list_for_each_safe(output, tmp, pending->outputs, link) {
    wl_list_remove(&output->link);
    free(output);
  }
  free(pending->outputs);
  free(pending);
}

static void config_handle_succeeded(void *data,
    struct zwlr_output_configuration_v1 *config) {
  struct wd_pending_config *pending = data;
  zwlr_output_configuration_v1_destroy(config);
  wd_ui_apply_done(pending->state, pending->outputs);
  if (store_config(pending->outputs) == 0)
  {
    wd_ui_show_error(pending->state,
      "Change was applied successfully and config was saved.");
  }
  destroy_pending(pending);
}

static void config_handle_failed(void *data,
    struct zwlr_output_configuration_v1 *config) {
  struct wd_pending_config *pending = data;
  zwlr_output_configuration_v1_destroy(config);
  wd_ui_apply_done(pending->state, NULL);
  wd_ui_show_error(pending->state,
      "The display server was not able to process your changes.");
  destroy_pending(pending);
}

static void config_handle_cancelled(void *data,
    struct zwlr_output_configuration_v1 *config) {
  struct wd_pending_config *pending = data;
  zwlr_output_configuration_v1_destroy(config);
  wd_ui_apply_done(pending->state, NULL);
  wd_ui_show_error(pending->state,
      "The display configuration was modified by the server before updates were processed. "
      "Please check the configuration and apply the changes again.");
  destroy_pending(pending);
}

static const struct zwlr_output_configuration_v1_listener config_listener = {
  .succeeded = config_handle_succeeded,
  .failed = config_handle_failed,
  .cancelled = config_handle_cancelled,
};

void wd_apply_state(struct wd_state *state, struct wl_list *new_outputs,
    struct wl_display *display) {
  struct zwlr_output_configuration_v1 *config =
    zwlr_output_manager_v1_create_configuration(state->output_manager, state->serial);

  struct wd_pending_config *pending = calloc(1, sizeof(*pending));
  pending->state = state;
  pending->outputs = new_outputs;

  zwlr_output_configuration_v1_add_listener(config, &config_listener, pending);

  ssize_t i = -1;
  struct wd_head_config *output;
  wl_list_for_each(output, new_outputs, link) {
    i++;
    struct wd_head *head = output->head;

    if (!output->enabled && output->enabled != head->enabled) {
      zwlr_output_configuration_v1_disable_head(config, head->wlr_head);
      continue;
    }

    struct zwlr_output_configuration_head_v1 *config_head = zwlr_output_configuration_v1_enable_head(config, head->wlr_head);

    const struct wd_mode *selected_mode = NULL;
    const struct wd_mode *mode;
    wl_list_for_each(mode, &head->modes, link) {
      if (mode->width == output->width && mode->height == output->height && mode->refresh == output->refresh) {
        selected_mode = mode;
        break;
      }
    }
    if (selected_mode != NULL) {
      if (output->enabled != head->enabled || selected_mode != head->mode) {
        zwlr_output_configuration_head_v1_set_mode(config_head, selected_mode->wlr_mode);
      }
    } else if (output->enabled != head->enabled
        || output->width != head->custom_mode.width
        || output->height != head->custom_mode.height
        || output->refresh != head->custom_mode.refresh) {
      zwlr_output_configuration_head_v1_set_custom_mode(config_head,
          output->width, output->height, output->refresh);
    }
    if (output->enabled != head->enabled || output->x != head->x || output->y != head->y) {
      zwlr_output_configuration_head_v1_set_position(config_head, output->x, output->y);
    }
    if (output->enabled != head->enabled || output->scale != head->scale) {
      zwlr_output_configuration_head_v1_set_scale(config_head, wl_fixed_from_double(output->scale));
    }
    if (output->enabled != head->enabled || output->transform != head->transform) {
      zwlr_output_configuration_head_v1_set_transform(config_head, output->transform);
    }
  }

  zwlr_output_configuration_v1_apply(config);

  wl_display_roundtrip(display);
}

static void wd_frame_destroy(struct wd_frame *frame) {
  if (frame->pixels != NULL)
    munmap(frame->pixels, frame->height * frame->stride);
  if (frame->buffer != NULL)
    wl_buffer_destroy(frame->buffer);
  if (frame->pool != NULL)
    wl_shm_pool_destroy(frame->pool);
  if (frame->capture_fd != -1)
    close(frame->capture_fd);
  if (frame->wlr_frame != NULL)
    zwlr_screencopy_frame_v1_destroy(frame->wlr_frame);

  wl_list_remove(&frame->link);
  free(frame);
}

static int create_shm_file(size_t size, const char *fmt, ...) {
  char *shm_name = NULL;
  int fd = -1;

  va_list vl;
  va_start(vl, fmt);
  int result = vasprintf(&shm_name, fmt, vl);
  va_end(vl);

  if (result == -1) {
    fprintf(stderr, "asprintf: %s\n", strerror(errno));
    shm_name = NULL;
    return -1;
  }

  fd = shm_open(shm_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
  if (fd == -1) {
    fprintf(stderr, "shm_open: %s\n", strerror(errno));
    free(shm_name);
    return -1;
  }
  shm_unlink(shm_name);
  free(shm_name);

  if (ftruncate(fd, size) == -1) {
    fprintf(stderr, "ftruncate: %s\n", strerror(errno));
    close(fd);
    return -1;
  }
  return fd;
}

static void capture_buffer(void *data,
    struct zwlr_screencopy_frame_v1 *copy_frame,
    uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
  struct wd_frame *frame = data;

  if (format != WL_SHM_FORMAT_ARGB8888 && format != WL_SHM_FORMAT_XRGB8888 &&
      format != WL_SHM_FORMAT_ABGR8888 && format != WL_SHM_FORMAT_XBGR8888) {
    goto err;
  }

  size_t size = stride * height;
  frame->capture_fd = create_shm_file(size, "/wd-%s", frame->output->name);
  if (frame->capture_fd == -1) {
    goto err;
  }

  frame->pool = wl_shm_create_pool(frame->output->state->shm,
      frame->capture_fd, size);
  frame->buffer = wl_shm_pool_create_buffer(frame->pool, 0,
      width, height, stride, format);
  zwlr_screencopy_frame_v1_copy(copy_frame, frame->buffer);
  frame->stride = stride;
  frame->width = width;
  frame->height = height;
  frame->swap_rgb = format == WL_SHM_FORMAT_ABGR8888
    || format == WL_SHM_FORMAT_XBGR8888;

  return;
err:
  wd_frame_destroy(frame);
}

static void capture_flags(void *data,
    struct zwlr_screencopy_frame_v1 *wlr_frame,
    uint32_t flags) {
  struct wd_frame *frame = data;
  frame->y_invert = !!(flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT);
}

static void capture_ready(void *data,
    struct zwlr_screencopy_frame_v1 *wlr_frame,
    uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
  struct wd_frame *frame = data;

  frame->pixels = mmap(NULL, frame->stride * frame->height,
      PROT_READ, MAP_SHARED, frame->capture_fd, 0);
  if (frame->pixels == MAP_FAILED) {
    frame->pixels = NULL;
    fprintf(stderr, "mmap: %d: %s\n", frame->capture_fd, strerror(errno));
    wd_frame_destroy(frame);
    return;
  } else {
    uint64_t tv_sec = (uint64_t) tv_sec_hi << 32 | tv_sec_lo;
    frame->tick = (tv_sec * 1000000) + (tv_nsec / 1000);
  }

  zwlr_screencopy_frame_v1_destroy(frame->wlr_frame);
  frame->wlr_frame = NULL;

  struct wd_frame *frame_iter, *frame_tmp;
  wl_list_for_each_safe(frame_iter, frame_tmp, &frame->output->frames, link) {
    if (frame != frame_iter) {
      wd_frame_destroy(frame_iter);
    }
  }
}

static void capture_failed(void *data,
    struct zwlr_screencopy_frame_v1 *wlr_frame) {
  struct wd_frame *frame = data;
  wd_frame_destroy(frame);
}

struct zwlr_screencopy_frame_v1_listener capture_listener = {
  .buffer = capture_buffer,
  .flags = capture_flags,
  .ready = capture_ready,
  .failed = capture_failed
};

static bool has_pending_captures(struct wd_state *state) {
  struct wd_output *output;
  wl_list_for_each(output, &state->outputs, link) {
    struct wd_frame *frame;
    wl_list_for_each(frame, &output->frames, link) {
      if (frame->pixels == NULL) {
        return true;
      }
    }
  }
  return false;
}

void wd_capture_frame(struct wd_state *state) {
  if (state->copy_manager == NULL || has_pending_captures(state)
      || !state->capture) {
    return;
  }

  struct wd_output *output;
  wl_list_for_each(output, &state->outputs, link) {
    struct wd_frame *frame = calloc(1, sizeof(*frame));
    frame->output = output;
    frame->capture_fd = -1;
    frame->wlr_frame =
      zwlr_screencopy_manager_v1_capture_output(state->copy_manager, 1,
        output->wl_output);
    zwlr_screencopy_frame_v1_add_listener(frame->wlr_frame, &capture_listener,
        frame);
    wl_list_insert(&output->frames, &frame->link);
  }
}

static void wd_output_destroy(struct wd_output *output) {
  struct wd_frame *frame, *frame_tmp;
  wl_list_for_each_safe(frame, frame_tmp, &output->frames, link) {
    wd_frame_destroy(frame);
  }
  if (output->state->layer_shell != NULL) {
    wd_destroy_overlay(output);
  }
  zxdg_output_v1_destroy(output->xdg_output);
  free(output->name);
  free(output);
}

static void wd_mode_destroy(struct wd_mode* mode) {
  zwlr_output_mode_v1_destroy(mode->wlr_mode);
  free(mode);
}

static void wd_head_destroy(struct wd_head *head) {
  if (head->state->clicked == head->render) {
    head->state->clicked = NULL;
  }
  if (head->render != NULL) {
    wl_list_remove(&head->render->link);
    free(head->render);
    head->render = NULL;
  }
  struct wd_mode *mode, *mode_tmp;
  wl_list_for_each_safe(mode, mode_tmp, &head->modes, link) {
    zwlr_output_mode_v1_destroy(mode->wlr_mode);
    free(mode);
  }
  zwlr_output_head_v1_destroy(head->wlr_head);
  free(head->name);
  free(head->description);
  free(head);
}

static void mode_handle_size(void *data, struct zwlr_output_mode_v1 *wlr_mode,
    int32_t width, int32_t height) {
  struct wd_mode *mode = data;
  mode->width = width;
  mode->height = height;
}

static void mode_handle_refresh(void *data,
    struct zwlr_output_mode_v1 *wlr_mode, int32_t refresh) {
  struct wd_mode *mode = data;
  mode->refresh = refresh;
}

static void mode_handle_preferred(void *data,
    struct zwlr_output_mode_v1 *wlr_mode) {
  struct wd_mode *mode = data;
  mode->preferred = true;
}

static void mode_handle_finished(void *data,
    struct zwlr_output_mode_v1 *wlr_mode) {
  struct wd_mode *mode = data;
  wl_list_remove(&mode->link);
  wd_mode_destroy(mode);
}

static const struct zwlr_output_mode_v1_listener mode_listener = {
  .size = mode_handle_size,
  .refresh = mode_handle_refresh,
  .preferred = mode_handle_preferred,
  .finished = mode_handle_finished,
};

static void head_handle_name(void *data,
    struct zwlr_output_head_v1 *wlr_head, const char *name) {
  struct wd_head *head = data;
  head->name = strdup(name);
  wd_ui_reset_head(head, WD_FIELD_NAME);
}

static void head_handle_description(void *data,
    struct zwlr_output_head_v1 *wlr_head, const char *description) {
  struct wd_head *head = data;
  head->description = strdup(description);
  wd_ui_reset_head(head, WD_FIELD_DESCRIPTION);
}

static void head_handle_physical_size(void *data,
    struct zwlr_output_head_v1 *wlr_head, int32_t width, int32_t height) {
  struct wd_head *head = data;
  head->phys_width = width;
  head->phys_height = height;
  wd_ui_reset_head(head, WD_FIELD_PHYSICAL_SIZE);
}

static void head_handle_mode(void *data,
    struct zwlr_output_head_v1 *wlr_head,
    struct zwlr_output_mode_v1 *wlr_mode) {
  struct wd_head *head = data;

  struct wd_mode *mode = calloc(1, sizeof(*mode));
  mode->head = head;
  mode->wlr_mode = wlr_mode;
  wl_list_insert(head->modes.prev, &mode->link);

  zwlr_output_mode_v1_add_listener(wlr_mode, &mode_listener, mode);
}

static void head_handle_enabled(void *data,
    struct zwlr_output_head_v1 *wlr_head, int32_t enabled) {
  struct wd_head *head = data;
  head->enabled = !!enabled;
  if (!enabled) {
    head->output = NULL;
  }
  wd_ui_reset_head(head, WD_FIELD_ENABLED);
}

static void head_handle_current_mode(void *data,
    struct zwlr_output_head_v1 *wlr_head,
    struct zwlr_output_mode_v1 *wlr_mode) {
  struct wd_head *head = data;
  struct wd_mode *mode;
  wl_list_for_each(mode, &head->modes, link) {
    if (mode->wlr_mode == wlr_mode) {
      head->mode = mode;
      wd_ui_reset_head(head, WD_FIELD_MODE);
      return;
    }
  }
  fprintf(stderr, "received unknown current_mode\n");
  head->mode = NULL;
}

static void head_handle_position(void *data,
    struct zwlr_output_head_v1 *wlr_head, int32_t x, int32_t y) {
  struct wd_head *head = data;
  head->x = x;
  head->y = y;
  wd_ui_reset_head(head, WD_FIELD_POSITION);
}

static void head_handle_transform(void *data,
    struct zwlr_output_head_v1 *wlr_head, int32_t transform) {
  struct wd_head *head = data;
  head->transform = transform;
  wd_ui_reset_head(head, WD_FIELD_TRANSFORM);
}

static void head_handle_scale(void *data,
    struct zwlr_output_head_v1 *wlr_head, wl_fixed_t scale) {
  struct wd_head *head = data;
  head->scale = wl_fixed_to_double(scale);
  wd_ui_reset_head(head, WD_FIELD_SCALE);
}

static void head_handle_finished(void *data,
    struct zwlr_output_head_v1 *wlr_head) {
  struct wd_head *head = data;
  struct wd_state *state = head->state;
  wl_list_remove(&head->link);
  wd_head_destroy(head);

  uint32_t counter = 0;
  wl_list_for_each(head, &state->heads, link) {
    if (head->id != counter) {
      head->id = counter;
      if (head->output != NULL) {
        wd_redraw_overlay(head->output);
      }
    }
    counter++;
  }
}

static const struct zwlr_output_head_v1_listener head_listener = {
  .name = head_handle_name,
  .description = head_handle_description,
  .physical_size = head_handle_physical_size,
  .mode = head_handle_mode,
  .enabled = head_handle_enabled,
  .current_mode = head_handle_current_mode,
  .position = head_handle_position,
  .transform = head_handle_transform,
  .scale = head_handle_scale,
  .finished = head_handle_finished,
};

static void output_manager_handle_head(void *data,
    struct zwlr_output_manager_v1 *manager,
    struct zwlr_output_head_v1 *wlr_head) {
  struct wd_state *state = data;

  struct wd_head *head = calloc(1, sizeof(*head));
  head->state = state;
  head->wlr_head = wlr_head;
  head->scale = 1.0;
  head->id = wl_list_length(&state->heads);
  wl_list_init(&head->modes);
  wl_list_insert(&state->heads, &head->link);

  zwlr_output_head_v1_add_listener(wlr_head, &head_listener, head);
}

static void output_manager_handle_done(void *data,
    struct zwlr_output_manager_v1 *manager, uint32_t serial) {
  struct wd_state *state = data;
  state->serial = serial;

  assert(wl_list_length(&state->heads) <= HEADS_MAX);

  struct wd_head *head = data;
  wl_list_for_each(head, &state->heads, link) {
    if (!head->enabled && head->mode == NULL && !wl_list_empty(&head->modes)) {
      struct wd_mode *mode = wl_container_of(head->modes.prev, mode, link);
      head->custom_mode.width = mode->width;
      head->custom_mode.height = mode->height;
      head->custom_mode.refresh = mode->refresh;
    }
  }
  wd_ui_reset_heads(state);
}

static const struct zwlr_output_manager_v1_listener output_manager_listener = {
  .head = output_manager_handle_head,
  .done = output_manager_handle_done,
  .finished = noop,
};
static void registry_handle_global(void *data, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version) {
  struct wd_state *state = data;

  if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0) {
    state->output_manager = wl_registry_bind(registry, name,
        &zwlr_output_manager_v1_interface, 1);
    zwlr_output_manager_v1_add_listener(state->output_manager,
        &output_manager_listener, state);
  } else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
    state->xdg_output_manager = wl_registry_bind(registry, name,
        &zxdg_output_manager_v1_interface, 2);
  } else if(strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
    state->copy_manager = wl_registry_bind(registry, name,
        &zwlr_screencopy_manager_v1_interface, 1);
  } else if(strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
    state->layer_shell = wl_registry_bind(registry, name,
        &zwlr_layer_shell_v1_interface, 1);
  } else if(strcmp(interface, wl_shm_interface.name) == 0) {
    state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
  }
}

static const struct wl_registry_listener registry_listener = {
  .global = registry_handle_global,
  .global_remove = noop,
};

void wd_add_output_management_listener(struct wd_state *state, struct
    wl_display *display) {
  struct wl_registry *registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, state);

  wl_display_dispatch(display);
  wl_display_roundtrip(display);
}

struct wd_head *wd_find_head(struct wd_state *state,
    struct wd_output *output) {
  struct wd_head *head;
  wl_list_for_each(head, &state->heads, link) {
    if (output->name != NULL && strcmp(output->name, head->name) == 0) {
      head->output = output;
      return head;
    }
  }
  return NULL;
}

static void output_logical_position(void *data, struct zxdg_output_v1 *zxdg_output_v1,
    int32_t x, int32_t y) {
  struct wd_output *output = data;
  struct wd_head *head = wd_find_head(output->state, output);
  if (head != NULL) {
    head->x = x;
    head->y = y;
    wd_ui_reset_head(head, WD_FIELD_POSITION);
  }
}

static void output_name(void *data, struct zxdg_output_v1 *zxdg_output_v1,
    const char *name) {
  struct wd_output *output = data;
  if (output->name != NULL) {
    free(output->name);
  }
  output->name = strdup(name);
  struct wd_head *head = wd_find_head(output->state, output);
  if (head != NULL) {
    wd_ui_reset_head(head, WD_FIELD_NAME);
  }
}

static const struct zxdg_output_v1_listener output_listener = {
  .logical_position = output_logical_position,
  .logical_size = noop,
  .done = noop,
  .name = output_name,
  .description = noop
};

void wd_add_output(struct wd_state *state, struct wl_output *wl_output,
    struct wl_display *display) {
  struct wd_output *output = calloc(1, sizeof(*output));
  output->state = state;
  output->wl_output = wl_output;
  output->xdg_output = zxdg_output_manager_v1_get_xdg_output(
      state->xdg_output_manager, wl_output);
  wl_list_init(&output->frames);
  zxdg_output_v1_add_listener(output->xdg_output, &output_listener, output);
  wl_list_insert(output->state->outputs.prev, &output->link);
  if (state->layer_shell != NULL && state->show_overlay) {
    wl_display_roundtrip(display);
    wd_create_overlay(output);
  }
}

void wd_remove_output(struct wd_state *state, struct wl_output *wl_output,
    struct wl_display *display) {
  struct wd_output *output, *output_tmp;
  wl_list_for_each_safe(output, output_tmp, &state->outputs, link) {
    if (output->wl_output == wl_output) {
      wl_list_remove(&output->link);
      wd_output_destroy(output);
      break;
    }
  }
  wd_capture_wait(state, display);
}

struct wd_output *wd_find_output(struct wd_state *state, struct wd_head
    *head) {
  if (!head->enabled) {
    return NULL;
  }
  if (head->output != NULL) {
    return head->output;
  }
  struct wd_output *output;
  wl_list_for_each(output, &state->outputs, link) {
    if (output->name != NULL && strcmp(output->name, head->name) == 0) {
      head->output = output;
      return output;
    }
  }
  head->output = NULL;
  return NULL;
}

struct wd_state *wd_state_create(void) {
  struct wd_state *state = calloc(1, sizeof(*state));
  state->zoom = 1.;
  state->capture = true;
  state->show_overlay = true;
  wl_list_init(&state->heads);
  wl_list_init(&state->outputs);
  wl_list_init(&state->render.heads);
  return state;
}

void wd_capture_wait(struct wd_state *state, struct wl_display *display) {
  wl_display_flush(display);
  while (has_pending_captures(state)) {
    if (wl_display_dispatch(display) == -1) {
      break;
    }
  }
}

void wd_state_destroy(struct wd_state *state) {
  struct wd_head *head, *head_tmp;
  wl_list_for_each_safe(head, head_tmp, &state->heads, link) {
    wd_head_destroy(head);
  }
  struct wd_output *output, *output_tmp;
  wl_list_for_each_safe(output, output_tmp, &state->outputs, link) {
    wd_output_destroy(output);
  }
  if (state->layer_shell != NULL) {
    zwlr_layer_shell_v1_destroy(state->layer_shell);
  }
  if (state->copy_manager != NULL) {
    zwlr_screencopy_manager_v1_destroy(state->copy_manager);
  }
  zwlr_output_manager_v1_destroy(state->output_manager);
  zxdg_output_manager_v1_destroy(state->xdg_output_manager);
  wl_shm_destroy(state->shm);
  free(state);
}
