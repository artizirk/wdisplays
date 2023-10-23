/* SPDX-FileCopyrightText: 2020 Jason Francis <jason@cycles.network>
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef HDISPLAY_HEADFORM_H
#define HDISPLAY_HEADFORM_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

enum wd_head_fields {
  WD_FIELD_NAME           = 1 << 0,
  WD_FIELD_ENABLED        = 1 << 1,
  WD_FIELD_DESCRIPTION    = 1 << 2,
  WD_FIELD_PHYSICAL_SIZE  = 1 << 3,
  WD_FIELD_SCALE          = 1 << 4,
  WD_FIELD_POSITION       = 1 << 5,
  WD_FIELD_MODE           = 1 << 6,
  WD_FIELD_TRANSFORM      = 1 << 7,
  WD_FIELDS_ALL           = (1 << 8) - 1
};

#define WD_TYPE_HEAD_FORM (wd_head_form_get_type())
G_DECLARE_DERIVABLE_TYPE(
    WdHeadForm, wd_head_form, WD, HEAD_FORM, GtkGrid)

struct _WdHeadFormClass {
  GtkGridClass parent_class;

  void (*changed)(WdHeadForm *form, enum wd_head_fields fields);
};

struct wd_head;
struct wd_head_config;

typedef struct _WdHeadDimensions {
  gdouble x;
  gdouble y;
  gdouble w;
  gdouble h;
  gdouble scale;
  int rotation_id;
  gboolean flipped;
} WdHeadDimensions;

GtkWidget *wd_head_form_new(void);

gboolean wd_head_form_get_enabled(WdHeadForm *form);
gboolean wd_head_form_has_changes(WdHeadForm *form, const struct wd_head *head);
void wd_head_form_update(WdHeadForm *form, const struct wd_head *head,
    enum wd_head_fields fields);
void wd_head_form_fill_config(WdHeadForm *form, struct wd_head_config *output);
void wd_head_form_get_dimensions(WdHeadForm *form, WdHeadDimensions *dimensions);
void wd_head_form_set_position(WdHeadForm *form, double x, double y);

G_END_DECLS

#endif

