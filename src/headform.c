/* SPDX-FileCopyrightText: 2020 Jason Francis <jason@cycles.network>
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "headform.h"
#include "hypr-display.h"

typedef struct _WdHeadFormPrivate {
  GtkWidget *enabled;
  GtkWidget *description;
  GtkWidget *physical_size;
  GtkWidget *scale;
  GtkWidget *pos_x;
  GtkWidget *pos_y;
  GtkWidget *width;
  GtkWidget *height;
  GtkWidget *refresh;
  GtkWidget *mode_button;
  GtkWidget *rotate_button;
  GtkWidget *flipped;

  GAction *mode_action;
  GAction *rotate_action;
} WdHeadFormPrivate;

enum {
  CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE(WdHeadForm, wd_head_form, GTK_TYPE_GRID,
    G_ADD_PRIVATE(WdHeadForm))

static const char *HEAD_PREFIX = "head";
static const char *MODE_PREFIX = "mode";
static const char *ROTATE_PREFIX = "rotate";

static void head_form_update_sensitivity(WdHeadForm *form) {
  WdHeadFormPrivate *priv = wd_head_form_get_instance_private(form);

  bool enabled_toggled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->enabled));

  g_autoptr(GList) children = gtk_container_get_children(GTK_CONTAINER(form));
  for (GList *child = children; child != NULL; child = child->next) {
    GtkWidget *widget = GTK_WIDGET(child->data);
    if (widget != priv->enabled) {
      gtk_widget_set_sensitive(widget, enabled_toggled);
    }
  }
}

static GVariant *create_mode_variant(int32_t w, int32_t h, int32_t r) {
  GVariant * const children[] = {
    g_variant_new_int32(w),
    g_variant_new_int32(h),
    g_variant_new_int32(r),
  };
  return g_variant_new_tuple(children, G_N_ELEMENTS(children));
}

struct vid_mode {
  int32_t width;
  int32_t height;
  int32_t refresh;
};

static void unpack_mode_variant(GVariant *value, struct vid_mode *mode) {
  g_autoptr(GVariant) width = g_variant_get_child_value(value, 0);
  mode->width = g_variant_get_int32(width);
  g_autoptr(GVariant) height = g_variant_get_child_value(value, 1);
  mode->height = g_variant_get_int32(height);
  g_autoptr(GVariant) refresh = g_variant_get_child_value(value, 2);
  mode->refresh = g_variant_get_int32(refresh);
}

static void enabled_toggled(GtkToggleButton *toggle, gpointer data) {
  WdHeadForm *form = WD_HEAD_FORM(data);
  head_form_update_sensitivity(form);
  g_signal_emit(form, signals[CHANGED], 0, WD_FIELD_ENABLED);
}

static void mode_spin_changed(GtkSpinButton *spin_button, gpointer data) {
  WdHeadForm *form = WD_HEAD_FORM(data);
  WdHeadFormPrivate *priv = wd_head_form_get_instance_private(form);
  struct vid_mode mode;
  GVariant *value = g_action_get_state(priv->mode_action);
  unpack_mode_variant(value, &mode);
  if (strcmp(gtk_widget_get_name(GTK_WIDGET(spin_button)), "width") == 0) {
    mode.width = gtk_spin_button_get_value(spin_button);
  } else if (strcmp(gtk_widget_get_name(GTK_WIDGET(spin_button)), "height") == 0) {
    mode.height = gtk_spin_button_get_value(spin_button);
  } else if (strcmp(gtk_widget_get_name(GTK_WIDGET(spin_button)), "refresh") == 0) {
    mode.refresh = gtk_spin_button_get_value(spin_button) * 1000.;
  }
  g_action_activate(priv->mode_action, create_mode_variant(mode.width, mode.height, mode.refresh));
  g_signal_emit(form, signals[CHANGED], 0, WD_FIELD_MODE);
}

static void position_spin_changed(GtkSpinButton *spin_button, gpointer data) {
  WdHeadForm *form = WD_HEAD_FORM(data);
  g_signal_emit(form, signals[CHANGED], 0, WD_FIELD_POSITION);
}

static void flipped_toggled(GtkToggleButton *toggle, gpointer data) {
  WdHeadForm *form = WD_HEAD_FORM(data);
  g_signal_emit(form, signals[CHANGED], 0, WD_FIELD_TRANSFORM);
}

static void wd_head_form_class_init(WdHeadFormClass *class) {
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(class);

  signals[CHANGED] = g_signal_new("changed",
      G_OBJECT_CLASS_TYPE(class),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(WdHeadFormClass, changed),
      NULL, NULL, NULL,
      G_TYPE_NONE, 1, G_TYPE_INT);

  gtk_widget_class_set_template_from_resource(widget_class,
     hypr-display_RESOURCE_PREFIX "/head.ui");

  gtk_widget_class_bind_template_callback(widget_class, enabled_toggled);
  gtk_widget_class_bind_template_callback(widget_class, mode_spin_changed);
  gtk_widget_class_bind_template_callback(widget_class, position_spin_changed);
  gtk_widget_class_bind_template_callback(widget_class, flipped_toggled);

  gtk_widget_class_bind_template_child_private(widget_class, WdHeadForm, enabled);
  gtk_widget_class_bind_template_child_private(widget_class, WdHeadForm, description);
  gtk_widget_class_bind_template_child_private(widget_class, WdHeadForm, physical_size);
  gtk_widget_class_bind_template_child_private(widget_class, WdHeadForm, scale);
  gtk_widget_class_bind_template_child_private(widget_class, WdHeadForm, pos_x);
  gtk_widget_class_bind_template_child_private(widget_class, WdHeadForm, pos_y);
  gtk_widget_class_bind_template_child_private(widget_class, WdHeadForm, width);
  gtk_widget_class_bind_template_child_private(widget_class, WdHeadForm, height);
  gtk_widget_class_bind_template_child_private(widget_class, WdHeadForm, refresh);
  gtk_widget_class_bind_template_child_private(widget_class, WdHeadForm, mode_button);
  gtk_widget_class_bind_template_child_private(widget_class, WdHeadForm, rotate_button);
  gtk_widget_class_bind_template_child_private(widget_class, WdHeadForm, flipped);
  gtk_widget_class_set_css_name(widget_class, "wd-head-form");
}

static int32_t get_rotate_value(enum wl_output_transform transform) {
  if (transform == WL_OUTPUT_TRANSFORM_90 || transform == WL_OUTPUT_TRANSFORM_FLIPPED_90) {
    return 90;
  } else if (transform == WL_OUTPUT_TRANSFORM_180 || transform == WL_OUTPUT_TRANSFORM_FLIPPED_180) {
    return 180;
  } else if (transform == WL_OUTPUT_TRANSFORM_270 || transform == WL_OUTPUT_TRANSFORM_FLIPPED_270) {
    return 270;
  }
  return 0;
}

static void rotate_selected(GSimpleAction *action, GVariant *param, gpointer data) {
  WdHeadForm *form = data;
  WdHeadFormPrivate *priv = wd_head_form_get_instance_private(form);
  GMenuModel *menu = gtk_menu_button_get_menu_model(GTK_MENU_BUTTON(priv->rotate_button));
  int items = g_menu_model_get_n_items(menu);
  for (int i = 0; i < items; i++) {
    g_autoptr(GVariant) target = g_menu_model_get_item_attribute_value(menu, i, G_MENU_ATTRIBUTE_TARGET, NULL);
    g_autoptr(GVariant) label = g_menu_model_get_item_attribute_value(menu, i, G_MENU_ATTRIBUTE_LABEL, NULL);
    if (g_variant_get_int32(target) == g_variant_get_int32(param)) {
      gtk_button_set_label(GTK_BUTTON(priv->rotate_button), g_variant_get_string(label, NULL));
      break;
    }
  }
  g_simple_action_set_state(action, param);
  g_signal_emit(form, signals[CHANGED], 0, WD_FIELD_TRANSFORM);
}

static void mode_selected(GSimpleAction *action, GVariant *param, gpointer data) {
  WdHeadForm *form = data;
  WdHeadFormPrivate *priv = wd_head_form_get_instance_private(form);
  struct vid_mode mode;
  unpack_mode_variant(param, &mode);

  g_simple_action_set_state(action, param);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->width), mode.width);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->height), mode.height);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->refresh), mode.refresh / 1000.);
  g_signal_emit(form, signals[CHANGED], 0, WD_FIELD_MODE);
}

static void wd_head_form_init(WdHeadForm *form) {
  gtk_widget_init_template(GTK_WIDGET(form));
  WdHeadFormPrivate *priv = wd_head_form_get_instance_private(form);

  GSimpleActionGroup *head_actions = g_simple_action_group_new();
  gtk_widget_insert_action_group(priv->mode_button, HEAD_PREFIX, G_ACTION_GROUP(head_actions));
  gtk_widget_insert_action_group(priv->rotate_button, HEAD_PREFIX, G_ACTION_GROUP(head_actions));

  GMenu *rotate_menu = g_menu_new();
  g_menu_append(rotate_menu, "Don't Rotate", "head.rotate(0)");
  g_menu_append(rotate_menu, "Rotate 90°", "head.rotate(90)");
  g_menu_append(rotate_menu, "Rotate 180°", "head.rotate(180)");
  g_menu_append(rotate_menu, "Rotate 270°", "head.rotate(270)");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(priv->rotate_button), G_MENU_MODEL(rotate_menu));
  gtk_menu_button_set_use_popover(GTK_MENU_BUTTON(priv->rotate_button), false);

  static const GVariantType * const mode_types[] = {
    G_VARIANT_TYPE_INT32,
    G_VARIANT_TYPE_INT32,
    G_VARIANT_TYPE_INT32
  };
  GSimpleAction *action = g_simple_action_new_stateful("mode",
      g_variant_type_new_tuple(mode_types, G_N_ELEMENTS(mode_types)),
      create_mode_variant(0, 0, 0));
  g_action_map_add_action(G_ACTION_MAP(head_actions), G_ACTION(action));
  g_signal_connect(action, "change-state", G_CALLBACK(mode_selected), form);
  g_object_unref(action);
  priv->mode_action = G_ACTION(action);

  action = g_simple_action_new_stateful(ROTATE_PREFIX, G_VARIANT_TYPE_INT32,
      g_variant_new_int32(0));
  g_action_map_add_action(G_ACTION_MAP(head_actions), G_ACTION(action));
  g_signal_connect(action, "change-state", G_CALLBACK(rotate_selected), form);
  g_object_unref(action);
  priv->rotate_action = G_ACTION(action);

  g_object_unref(head_actions);
}

void wd_head_form_update(WdHeadForm *form, const struct wd_head *head,
    enum wd_head_fields fields) {
  g_return_if_fail(form);
  g_return_if_fail(head);

  WdHeadFormPrivate *priv = wd_head_form_get_instance_private(form);
  if (!fields)
    return;

  if (fields & WD_FIELD_DESCRIPTION)
    gtk_label_set_text(GTK_LABEL(priv->description), head->description);
  if (fields & WD_FIELD_PHYSICAL_SIZE) {
    g_autofree gchar *physical_str = g_strdup_printf("%dmm × %dmm", head->phys_width, head->phys_height);
    gtk_label_set_text(GTK_LABEL(priv->physical_size), physical_str);
  }
  if (fields & WD_FIELD_ENABLED)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->enabled), head->enabled);
  if (fields & WD_FIELD_SCALE)
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->scale), head->scale);
  if (fields & WD_FIELD_POSITION) {
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->pos_x), head->x);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->pos_y), head->y);
  }

  if (fields & WD_FIELD_MODE) {
    GMenu *mode_menu = g_menu_new();
    struct wd_mode *mode;
    g_autofree gchar *action = g_strdup_printf("%s.%s", HEAD_PREFIX, MODE_PREFIX);
    wl_list_for_each(mode, &head->modes, link) {
      g_autofree gchar *name = g_strdup_printf("%d×%d@%0.3fHz", mode->width, mode->height, mode->refresh / 1000.);
      GMenuItem *item = g_menu_item_new(name, action);
      g_menu_item_set_attribute_value(item, G_MENU_ATTRIBUTE_TARGET,
          create_mode_variant(mode->width, mode->height, mode->refresh));
      g_menu_append_item(mode_menu, item);
    }
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(priv->mode_button), G_MENU_MODEL(mode_menu));
    gtk_menu_button_set_use_popover(GTK_MENU_BUTTON(priv->mode_button), false);
    // Mode entries
    int w = head->custom_mode.width;
    int h = head->custom_mode.height;
    int r = head->custom_mode.refresh;
    if (head->enabled && head->mode != NULL) {
      w = head->mode->width;
      h = head->mode->height;
      r = head->mode->refresh;
    } else if (!head->enabled && w == 0 && h == 0) {
      struct wd_mode *mode;
      wl_list_for_each(mode, &head->modes, link) {
        if (mode->preferred) {
          w = mode->width;
          h = mode->height;
          r = mode->refresh;
          break;
        }
      }
    }

    g_action_change_state(priv->mode_action, create_mode_variant(w, h, r));
  }

  if (fields & WD_FIELD_TRANSFORM) {
    int active_rotate = get_rotate_value(head->transform);
    g_action_change_state(priv->rotate_action, g_variant_new_int32(active_rotate));

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->flipped),
        head->transform == WL_OUTPUT_TRANSFORM_FLIPPED
        || head->transform == WL_OUTPUT_TRANSFORM_FLIPPED_90
        || head->transform == WL_OUTPUT_TRANSFORM_FLIPPED_180
        || head->transform == WL_OUTPUT_TRANSFORM_FLIPPED_270);
  }

  // Sync state
  if (fields & WD_FIELD_ENABLED) {
    head_form_update_sensitivity(form);
  }
  g_signal_emit(form, signals[CHANGED], 0);
}

GtkWidget *wd_head_form_new(void) {
  return gtk_widget_new(WD_TYPE_HEAD_FORM, NULL);
}

gboolean wd_head_form_get_enabled(WdHeadForm *form) {
  WdHeadFormPrivate *priv = wd_head_form_get_instance_private(form);
  return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->enabled));
}

gboolean wd_head_form_has_changes(WdHeadForm *form, const struct wd_head *head) {
  g_return_val_if_fail(form, FALSE);

  WdHeadFormPrivate *priv = wd_head_form_get_instance_private(form);
  if (head->enabled != gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->enabled))) {
    return TRUE;
  }
  double old_scale = round(head->scale * 100.) / 100.;
  double new_scale = round(gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->scale)) * 100.) / 100.;
  if (old_scale != new_scale) {
    return TRUE;
  }
  if (head->x != gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->pos_x))) {
    return TRUE;
  }
  if (head->y != gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->pos_y))) {
    return TRUE;
  }
  int w = head->mode != NULL ? head->mode->width : head->custom_mode.width;
  if (w != gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->width))) {
    return TRUE;
  }
  int h = head->mode != NULL ? head->mode->height : head->custom_mode.height;
  if (h != gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->height))) {
    return TRUE;
  }
  int r = head->mode != NULL ? head->mode->refresh : head->custom_mode.refresh;
  if (r / 1000. != gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->refresh))) {
    return TRUE;
  }
  if (g_variant_get_int32(g_action_get_state(priv->rotate_action)) != get_rotate_value(head->transform)) {
    return TRUE;
  }
  bool flipped = head->transform == WL_OUTPUT_TRANSFORM_FLIPPED
    || head->transform == WL_OUTPUT_TRANSFORM_FLIPPED_90
    || head->transform == WL_OUTPUT_TRANSFORM_FLIPPED_180
    || head->transform == WL_OUTPUT_TRANSFORM_FLIPPED_270;
  if (flipped != gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->flipped))) {
    return TRUE;
  }
  return FALSE;
}

void wd_head_form_fill_config(WdHeadForm *form, struct wd_head_config *output) {
  g_return_if_fail(form);
  g_return_if_fail(output);

  WdHeadFormPrivate *priv = wd_head_form_get_instance_private(form);
  output->enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->enabled));
  output->scale = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->scale));
  output->x = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->pos_x));
  output->y = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->pos_y));
  output->width = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->width));
  output->height = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->height));
  output->refresh = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->refresh)) * 1000.;
  int32_t rotate = g_variant_get_int32(g_action_get_state(priv->rotate_action));
  gboolean flipped = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->flipped));
  switch (rotate) {
    case 0: output->transform = flipped ? WL_OUTPUT_TRANSFORM_FLIPPED : WL_OUTPUT_TRANSFORM_NORMAL; break;
    case 90: output->transform = flipped ? WL_OUTPUT_TRANSFORM_FLIPPED_90 : WL_OUTPUT_TRANSFORM_90; break;
    case 180: output->transform = flipped ? WL_OUTPUT_TRANSFORM_FLIPPED_180 : WL_OUTPUT_TRANSFORM_180; break;
    case 270: output->transform = flipped ? WL_OUTPUT_TRANSFORM_FLIPPED_270 : WL_OUTPUT_TRANSFORM_270; break;
  }
}

void wd_head_form_get_dimensions(WdHeadForm *form, WdHeadDimensions *dimensions) {
  g_return_if_fail(form);
  g_return_if_fail(dimensions);

  WdHeadFormPrivate *priv = wd_head_form_get_instance_private(form);

  dimensions->x = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->pos_x));
  dimensions->y = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->pos_y));
  dimensions->w = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->width));
  dimensions->h = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->height));
  dimensions->scale = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->scale));
  dimensions->rotation_id = g_variant_get_int32(g_action_get_state(priv->rotate_action)) / 90;
  dimensions->flipped = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->flipped));
}

void wd_head_form_set_position(WdHeadForm *form, double x, double y) {
  g_return_if_fail(form);
  WdHeadFormPrivate *priv = wd_head_form_get_instance_private(form);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->pos_x), x);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->pos_y), y);
}
