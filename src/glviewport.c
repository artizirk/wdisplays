/* SPDX-FileCopyrightText: 2020 Jason Francis <jason@cycles.network>
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "glviewport.h"

typedef struct _WdGLViewportPrivate {
  GtkAdjustment  *hadjustment;
  GtkAdjustment  *vadjustment;
  guint hscroll_policy : 1;
  guint vscroll_policy : 1;
} WdGLViewportPrivate;

enum {
  PROP_0,
  PROP_HADJUSTMENT,
  PROP_VADJUSTMENT,
  PROP_HSCROLL_POLICY,
  PROP_VSCROLL_POLICY
};

static void wd_gl_viewport_set_property(
    GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void wd_gl_viewport_get_property(
    GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

G_DEFINE_TYPE_WITH_CODE(WdGLViewport, wd_gl_viewport, GTK_TYPE_GL_AREA,
    G_ADD_PRIVATE(WdGLViewport)
    G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, NULL))

static void wd_gl_viewport_finalize(GObject *object) {
  WdGLViewport *viewport = WD_GL_VIEWPORT(object);
  WdGLViewportPrivate *priv = wd_gl_viewport_get_instance_private(viewport);

  g_clear_object(&priv->hadjustment);
  g_clear_object(&priv->vadjustment);

  G_OBJECT_CLASS(wd_gl_viewport_parent_class)->finalize(object);
}

static void wd_gl_viewport_class_init(WdGLViewportClass *class) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(class);

  gobject_class->finalize = wd_gl_viewport_finalize;
  gobject_class->set_property = wd_gl_viewport_set_property;
  gobject_class->get_property = wd_gl_viewport_get_property;

  g_object_class_override_property(gobject_class, PROP_HADJUSTMENT, "hadjustment");
  g_object_class_override_property(gobject_class, PROP_VADJUSTMENT, "vadjustment");
  g_object_class_override_property(gobject_class, PROP_HSCROLL_POLICY, "hscroll-policy");
  g_object_class_override_property(gobject_class, PROP_VSCROLL_POLICY, "vscroll-policy");
}

static void viewport_set_adjustment(GtkAdjustment *adjustment,
    GtkAdjustment **store) {
  if (!adjustment) {
    adjustment = gtk_adjustment_new(0., 0., 0., 0., 0., 0.);
  }
  if (adjustment != *store) {
    if (*store != NULL) {
      g_object_unref(*store);
    }
    *store = adjustment;
    g_object_ref_sink(adjustment);
  }
}

static void wd_gl_viewport_set_property(
    GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
  WdGLViewport *viewport = WD_GL_VIEWPORT(object);
  WdGLViewportPrivate *priv = wd_gl_viewport_get_instance_private(viewport);

  switch (prop_id) {
  case PROP_HADJUSTMENT:
    viewport_set_adjustment(g_value_get_object(value), &priv->hadjustment);
    break;
  case PROP_VADJUSTMENT:
    viewport_set_adjustment(g_value_get_object(value), &priv->vadjustment);
    break;
  case PROP_HSCROLL_POLICY:
    if (priv->hscroll_policy != g_value_get_enum(value)) {
      priv->hscroll_policy = g_value_get_enum(value);
      g_object_notify_by_pspec(object, pspec);
    }
    break;
  case PROP_VSCROLL_POLICY:
    if (priv->vscroll_policy != g_value_get_enum(value)) {
      priv->vscroll_policy = g_value_get_enum(value);
      g_object_notify_by_pspec (object, pspec);
    }
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void wd_gl_viewport_get_property(
    GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
  WdGLViewport *viewport = WD_GL_VIEWPORT(object);
  WdGLViewportPrivate *priv = wd_gl_viewport_get_instance_private(viewport);

  switch (prop_id) {
  case PROP_HADJUSTMENT:
    g_value_set_object(value, priv->hadjustment);
    break;
  case PROP_VADJUSTMENT:
    g_value_set_object(value, priv->vadjustment);
    break;
  case PROP_HSCROLL_POLICY:
    g_value_set_enum(value, priv->hscroll_policy);
    break;
  case PROP_VSCROLL_POLICY:
    g_value_set_enum(value, priv->vscroll_policy);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void wd_gl_viewport_init(WdGLViewport *viewport) {
}

GtkWidget *wd_gl_viewport_new(void) {
  return gtk_widget_new(WD_TYPE_GL_VIEWPORT, NULL);
}
