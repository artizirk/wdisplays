/* SPDX-FileCopyrightText: 2020 Jason Francis <jason@cycles.network>
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef HDISPLAY_GLVIEWPORT_H
#define HDISPLAY_GLVIEWPORT_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define WD_TYPE_GL_VIEWPORT (wd_gl_viewport_get_type())
G_DECLARE_DERIVABLE_TYPE(
    WdGLViewport, wd_gl_viewport, WD, GL_VIEWPORT,GtkGLArea)

struct _WdGLViewportClass {
  GtkGLAreaClass parent_class;
};

GtkWidget *wd_gl_viewport_new(void);

G_END_DECLS

#endif
