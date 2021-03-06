/*
    This file is part of darktable,
    copyright (c) 2011 Henrik Andersson.
    copyright (c) 2016 tobias ellinghaus.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "common/darktable.h"
#include "common/debug.h"
#include "common/image_cache.h"
#include "control/conf.h"
#include "control/control.h"
#include "develop/develop.h"
#include "dtgtk/button.h"
#include "gui/gtk.h"
#include "libs/lib.h"
#include "libs/lib_api.h"

DT_MODULE(1)

enum
{
  TEXT_COLUMN,
  VIEW_COLUMN,
  SENSITIVE_COLUMN,
  N_COLUMNS
};

typedef struct dt_lib_viewswitcher_t
{
  GList *labels;
  GtkWidget *dropdown;
} dt_lib_viewswitcher_t;

/* callback when a view label is pressed */
static gboolean _lib_viewswitcher_button_press_callback(GtkWidget *w, GdkEventButton *ev, gpointer user_data);
/* helper function to create a label */
static GtkWidget *_lib_viewswitcher_create_label(dt_view_t *view);
/* callback when view changed signal happens */
static void _lib_viewswitcher_view_changed_callback(gpointer instance, dt_view_t *old_view,
                                                    dt_view_t *new_view, gpointer user_data);
static void _switch_view(int which);

const char *name(dt_lib_module_t *self)
{
  return _("viewswitcher");
}

uint32_t views(dt_lib_module_t *self)
{
  return DT_VIEW_ALL;
}

uint32_t container(dt_lib_module_t *self)
{
  return DT_UI_CONTAINER_PANEL_TOP_RIGHT;
}

int expandable(dt_lib_module_t *self)
{
  return 0;
}

int position()
{
  return 1001;
}

static void _dropdown_changed(GtkComboBox *widget, gpointer user_data)
{
  dt_lib_viewswitcher_t *d = (dt_lib_viewswitcher_t *)user_data;

  GtkTreeIter iter;
  if(gtk_combo_box_get_active_iter(GTK_COMBO_BOX(d->dropdown), &iter))
  {
    int view;
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(d->dropdown));
    gtk_tree_model_get(model, &iter, VIEW_COLUMN, &view, -1);
    _switch_view(view);
  }
}

void gui_init(dt_lib_module_t *self)
{
  /* initialize ui widgets */
  dt_lib_viewswitcher_t *d = (dt_lib_viewswitcher_t *)g_malloc0(sizeof(dt_lib_viewswitcher_t));
  self->data = (void *)d;

  self->widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, DT_PIXEL_APPLY_DPI(5));
  d->dropdown = NULL;
  GtkTreeIter iter;
  GtkListStore *model = NULL;

  for(int k = 0; k < darktable.view_manager->num_views; k++)
  {
    if(darktable.view_manager->view[k].module)
    {
      // lighttable and darkroom are shown in the top level, the rest in a dropdown
      /* create view label */
      dt_view_t *v = &darktable.view_manager->view[k];

      if(!g_strcmp0(v->module_name, "lighttable") || !g_strcmp0(v->module_name, "darkroom"))
      {
        GtkWidget *w = _lib_viewswitcher_create_label(v);
        gtk_box_pack_start(GTK_BOX(self->widget), w, FALSE, FALSE, 0);
        d->labels = g_list_append(d->labels, gtk_bin_get_child(GTK_BIN(w)));

        /* create space if more views */
        if(k < darktable.view_manager->num_views - 1)
        {
          GtkWidget *w = gtk_label_new("|");
          gtk_widget_set_halign(w, GTK_ALIGN_START);
          gtk_widget_set_name(w, "view_label");
          gtk_box_pack_start(GTK_BOX(self->widget), w, FALSE, FALSE, DT_PIXEL_APPLY_DPI(5));
        }
      }
      else
      {
        // only create the dropdown when needed, in case someone runs dt with just lt + dr
        if(!d->dropdown)
        {
          model = gtk_list_store_new(N_COLUMNS, G_TYPE_STRING, G_TYPE_INT, G_TYPE_BOOLEAN);
          d->dropdown = gtk_combo_box_new_with_model(GTK_TREE_MODEL(model));
          gtk_widget_set_name(d->dropdown, "view_dropdown");
          GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
          gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(d->dropdown), renderer, FALSE);
          gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(d->dropdown), renderer, "markup", TEXT_COLUMN,
                                         "sensitive", SENSITIVE_COLUMN, NULL);

          gtk_list_store_append(model, &iter);
//           char *italic = g_strdup_printf("<i>%s</i>", _("other"));
          gtk_list_store_set(model, &iter, TEXT_COLUMN, /*italic*/ _("other"), VIEW_COLUMN, 0, SENSITIVE_COLUMN, 0, -1);
//           g_free(italic);

          gtk_box_pack_start(GTK_BOX(self->widget), d->dropdown, FALSE, FALSE, 0);
          g_signal_connect(G_OBJECT(d->dropdown), "changed", G_CALLBACK(_dropdown_changed), d);
        }

        gtk_list_store_append(model, &iter);
        gtk_list_store_set(model, &iter, TEXT_COLUMN, v->name(v), VIEW_COLUMN, v->view(v), SENSITIVE_COLUMN, 1, -1);
      }
    }
  }

  if(model) g_object_unref(model);

  /* connect callback to view change signal */
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_VIEWMANAGER_VIEW_CHANGED,
                            G_CALLBACK(_lib_viewswitcher_view_changed_callback), self);
}

void gui_cleanup(dt_lib_module_t *self)
{
  dt_control_signal_disconnect(darktable.signals, G_CALLBACK(_lib_viewswitcher_view_changed_callback), self);
  g_free(self->data);
  self->data = NULL;
}

static void _lib_viewswitcher_enter_notify_callback(GtkWidget *w, GdkEventCrossing *e, gpointer user_data)
{
  GtkLabel *l = (GtkLabel *)user_data;

  /* if not active view lets highlight */
  if(strcmp(g_object_get_data(G_OBJECT(w), "view-label"), dt_view_manager_name(darktable.view_manager)))
  {
    gtk_widget_set_state_flags(GTK_WIDGET(l), GTK_STATE_FLAG_PRELIGHT, TRUE);
  }
}

static void _lib_viewswitcher_leave_notify_callback(GtkWidget *w, GdkEventCrossing *e, gpointer user_data)
{
  GtkLabel *l = (GtkLabel *)user_data;

  /* if not active view lets set default */
  if(strcmp(g_object_get_data(G_OBJECT(w), "view-label"), dt_view_manager_name(darktable.view_manager)))
  {
    gtk_widget_set_state_flags(GTK_WIDGET(l), GTK_STATE_FLAG_NORMAL, TRUE);
  }
}

static void _lib_viewswitcher_view_changed_callback(gpointer instance, dt_view_t *old_view,
                                                    dt_view_t *new_view, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_viewswitcher_t *d = (dt_lib_viewswitcher_t *)self->data;

  const char *name = dt_view_manager_name(darktable.view_manager);
  gboolean found = FALSE;

  for(GList *iter = d->labels; iter; iter = g_list_next(iter))
  {
    GtkWidget *label = GTK_WIDGET(iter->data);
    if(!g_strcmp0(g_object_get_data(G_OBJECT(label), "view-label"), name))
    {
      gtk_widget_set_state_flags(label, GTK_STATE_FLAG_SELECTED, TRUE);
      found = TRUE;
    }
    else
      gtk_widget_set_state_flags(label, GTK_STATE_FLAG_NORMAL, TRUE);
  }

  g_signal_handlers_block_by_func(d->dropdown, _dropdown_changed, d);

  if(found)
  {
    gtk_combo_box_set_active(GTK_COMBO_BOX(d->dropdown), 0);
    gtk_widget_set_state_flags(d->dropdown, GTK_STATE_FLAG_NORMAL, TRUE);
  }
  else
  {
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(d->dropdown));
    GtkTreeIter iter;
    uint32_t index = 0;
    if(gtk_tree_model_get_iter_first(model, &iter) == TRUE) do
    {
      gchar *str;
      gtk_tree_model_get(model, &iter, TEXT_COLUMN, &str, -1);
      if(!g_strcmp0(str, name))
      {
        gtk_combo_box_set_active(GTK_COMBO_BOX(d->dropdown), index);
        gtk_widget_set_state_flags(d->dropdown, GTK_STATE_FLAG_SELECTED, TRUE);
        break;
      }
      g_free(str);
      index++;
    } while(gtk_tree_model_iter_next(model, &iter) == TRUE);
  }

  g_signal_handlers_unblock_by_func(d->dropdown, _dropdown_changed, d);
}

static GtkWidget *_lib_viewswitcher_create_label(dt_view_t *v)
{
  GtkWidget *eb = gtk_event_box_new();
  GtkWidget *b = gtk_label_new(v->name(v));
  gtk_container_add(GTK_CONTAINER(eb), b);
  /*setup label*/
  gtk_widget_set_halign(b, GTK_ALIGN_START);
  g_object_set_data(G_OBJECT(b), "view-label", (gchar *)v->name(v));
  g_object_set_data(G_OBJECT(eb), "view-label", (gchar *)v->name(v));
  gtk_widget_set_name(b, "view_label");
  gtk_widget_set_state_flags(b, GTK_STATE_FLAG_NORMAL, TRUE);

  /* connect button press handler */
  g_signal_connect(G_OBJECT(eb), "button-press-event", G_CALLBACK(_lib_viewswitcher_button_press_callback),
                   GINT_TO_POINTER(v->view(v)));

  /* set enter/leave notify events and connect signals */
  gtk_widget_add_events(GTK_WIDGET(eb), GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);

  g_signal_connect(G_OBJECT(eb), "enter-notify-event", G_CALLBACK(_lib_viewswitcher_enter_notify_callback), b);
  g_signal_connect(G_OBJECT(eb), "leave-notify-event", G_CALLBACK(_lib_viewswitcher_leave_notify_callback), b);

  return eb;
}

static void _switch_view(int which)
{
  /* FIXME: get rid of these mappings and old DT_xxx */
  if(which == DT_VIEW_LIGHTTABLE)
    dt_ctl_switch_mode_to(DT_LIBRARY);
  else if(which == DT_VIEW_DARKROOM)
    dt_ctl_switch_mode_to(DT_DEVELOP);
#ifdef HAVE_GPHOTO2
  else if(which == DT_VIEW_TETHERING)
    dt_ctl_switch_mode_to(DT_CAPTURE);
#endif
#ifdef HAVE_MAP
  else if(which == DT_VIEW_MAP)
    dt_ctl_switch_mode_to(DT_MAP);
#endif
  else if(which == DT_VIEW_SLIDESHOW)
    dt_ctl_switch_mode_to(DT_SLIDESHOW);
#ifdef HAVE_PRINT
  else if (which == DT_VIEW_PRINT)
    dt_ctl_switch_mode_to(DT_PRINT);
#endif
}

static gboolean _lib_viewswitcher_button_press_callback(GtkWidget *w, GdkEventButton *ev, gpointer user_data)
{
  if(ev->button == 1)
  {
    int which = GPOINTER_TO_INT(user_data);
    _switch_view(which);
    return TRUE;
  }
  return FALSE;
}


// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
