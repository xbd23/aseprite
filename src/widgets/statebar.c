/* ASE - Allegro Sprite Editor
 * Copyright (C) 2001-2008  David A. Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <allegro.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "jinete/jinete.h"

#include "commands/commands.h"
#include "core/core.h"
#include "modules/editors.h"
#include "modules/gfx.h"
#include "modules/gui.h"
#include "modules/palettes.h"
#include "modules/sprites.h"
#include "raster/cel.h"
#include "raster/layer.h"
#include "raster/sprite.h"
#include "raster/undo.h"
#include "util/misc.h"
#include "widgets/editor.h"
#include "widgets/statebar.h"

enum {
  ACTION_LAYER,
  ACTION_FIRST,
  ACTION_PREV,
  ACTION_PLAY,
  ACTION_NEXT,
  ACTION_LAST,
};

static bool statusbar_msg_proc(JWidget widget, JMessage msg);
static bool tipwindow_msg_proc(JWidget widget, JMessage msg);

static bool slider_change_hook(JWidget widget, void *data);
static void button_command(JWidget widget, void *data);

static void update_from_layer(StatusBar *statusbar);

JWidget statusbar_new(void)
{
#define BUTTON_NEW(name, text, data)					\
  (name) = jbutton_new(text);						\
  (name)->user_data[0] = statusbar;					\
  jbutton_add_command_data((name), button_command, (void *)(data));

#define ICON_NEW(name, icon, action)					\
  BUTTON_NEW((name), NULL, (action));					\
  add_gfxicon_to_button((name), (icon), JI_CENTER | JI_MIDDLE);

  JWidget widget = jwidget_new(statusbar_type());
  StatusBar *statusbar = jnew(StatusBar, 1);

  jwidget_add_hook(widget, statusbar_type(),
		   statusbar_msg_proc, statusbar);
  jwidget_focusrest(widget, TRUE);

  {
    JWidget box1, box2;

    statusbar->widget = widget;
    statusbar->timeout = 0;
    statusbar->progress = jlist_new();
    statusbar->tipwindow = NULL;

    /* construct the commands box */
    box1 = jbox_new(JI_HORIZONTAL);
    box2 = jbox_new(JI_HORIZONTAL | JI_HOMOGENEOUS);
    BUTTON_NEW(statusbar->b_layer, "*Current Layer*", ACTION_LAYER);
    statusbar->slider = jslider_new(0, 255, 255);
    ICON_NEW(statusbar->b_first, GFX_ANI_FIRST, ACTION_FIRST);
    ICON_NEW(statusbar->b_prev, GFX_ANI_PREV, ACTION_PREV);
    ICON_NEW(statusbar->b_play, GFX_ANI_PLAY, ACTION_PLAY);
    ICON_NEW(statusbar->b_next, GFX_ANI_NEXT, ACTION_NEXT);
    ICON_NEW(statusbar->b_last, GFX_ANI_LAST, ACTION_LAST);

    HOOK(statusbar->slider, JI_SIGNAL_SLIDER_CHANGE, slider_change_hook, 0);
    jwidget_set_min_size(statusbar->slider, JI_SCREEN_W/5, 0);

    jwidget_noborders(box1);
    jwidget_noborders(box2);

    jwidget_expansive(statusbar->b_layer, TRUE);
    jwidget_add_child(box1, statusbar->b_layer);
    jwidget_add_child(box1, statusbar->slider);
    jwidget_add_child(box2, statusbar->b_first);
    jwidget_add_child(box2, statusbar->b_prev);
    jwidget_add_child(box2, statusbar->b_play);
    jwidget_add_child(box2, statusbar->b_next);
    jwidget_add_child(box2, statusbar->b_last);
    jwidget_add_child(box1, box2);

    statusbar->commands_box = box1;
  }

  return widget;
}

int statusbar_type(void)
{
  static int type = 0;
  if (!type)
    type = ji_register_widget_type();
  return type;
}

StatusBar *statusbar_data(JWidget widget)
{
  return jwidget_get_data(widget, statusbar_type());
}

void statusbar_set_text(JWidget widget, int msecs, const char *format, ...)
{
  StatusBar *statusbar = statusbar_data(widget);

  if ((ji_clock > statusbar->timeout) || (msecs > 0)) {
    char buf[256];		/* TODO warning buffer overflow */
    va_list ap;

    va_start(ap, format);
    vsprintf(buf, format, ap);
    va_end(ap);

    if (widget->text)
      jfree(widget->text);

    widget->text = buf ? jstrdup(buf) : NULL;
    statusbar->timeout = ji_clock + msecs;
    jwidget_dirty(widget);
  }
}

void statusbar_show_tip(JWidget widget, int msecs, const char *format, ...)
{
  StatusBar *statusbar = statusbar_data(widget);
  JWidget tipwindow = statusbar->tipwindow;
  char buf[256];		/* TODO warning buffer overflow */
  va_list ap;
  int x, y;

  va_start(ap, format);
  vsprintf(buf, format, ap);
  va_end(ap);

  if (tipwindow == NULL) {
    tipwindow = jtooltip_window_new(buf);
    tipwindow->user_data[0] = (void *)jmanager_add_timer(tipwindow, msecs);
    tipwindow->user_data[1] = statusbar;
    jwidget_add_hook(tipwindow, -1, tipwindow_msg_proc, NULL);

    statusbar->tipwindow = tipwindow;
  }
  else {
    jwidget_set_text(tipwindow, buf);

    jmanager_set_timer_interval((int)tipwindow->user_data[0], msecs);
  }

  if (jwidget_is_visible(tipwindow))
    jwindow_close(tipwindow, NULL);

  jwindow_open(tipwindow);
  jwindow_remap(tipwindow);

  x = widget->rc->x2 - jrect_w(tipwindow->rc);
  y = widget->rc->y1 - jrect_h(tipwindow->rc);
  jwindow_position(tipwindow, x, y);

  jmanager_start_timer((int)tipwindow->user_data[0]);
}

void statusbar_show_color(JWidget widget, int msecs, int imgtype, color_t color)
{
  char buf[128];		/* TODO warning buffer overflow */
  color_to_formalstring(imgtype, color, buf, sizeof(buf), TRUE);
  statusbar_set_text(widget, msecs, "%s %s", _("Color"), buf);
}

void statusbar_update(JWidget widget)
{
  StatusBar *statusbar = statusbar_data(widget);

  update_from_layer(statusbar);
}

Progress *progress_new(JWidget widget)
{
  Progress *progress = jnew(Progress, 1);
  if (!progress)
    return NULL;

  progress->statusbar = widget;
  progress->pos = 0.0f;

  jlist_append(statusbar_data(widget)->progress, progress);
  jwidget_dirty(widget);

  return progress;
}

void progress_free(Progress *progress)
{
  jlist_remove(statusbar_data(progress->statusbar)->progress,
	       progress);
  jwidget_dirty(progress->statusbar);

  jfree(progress);
}

void progress_update(Progress *progress, float progress_pos)
{
  if (progress->pos != progress_pos) {
    progress->pos = progress_pos;
    jwidget_dirty(progress->statusbar);
  }
}

static bool statusbar_msg_proc(JWidget widget, JMessage msg)
{
  StatusBar *statusbar = statusbar_data(widget);

  switch (msg->type) {

    case JM_DESTROY: {
      JLink link;

      JI_LIST_FOR_EACH(statusbar->progress, link) {
	jfree(link->data);
      }
      jlist_free(statusbar->progress);

      if (statusbar->tipwindow != NULL)
	jwidget_free(statusbar->tipwindow);

      jfree(statusbar);
      break;
    }

    case JM_REQSIZE:
      msg->reqsize.w =
	msg->reqsize.h = 2 + jwidget_get_text_height(widget) + 2;
      return TRUE;

    case JM_SETPOS:
      jrect_copy(widget->rc, &msg->setpos.rect);
      jwidget_set_rect(statusbar->commands_box, widget->rc);
      return TRUE;

    case JM_CLOSE:
      if (!jwidget_has_child(widget, statusbar->commands_box)) {
	/* append the "commands_box" to destroy it in the jwidget_free */
	jwidget_add_child(widget, statusbar->commands_box);
      }
      break;

    case JM_DRAW: {
      JRect rc = jwidget_get_rect(widget);

      jdraw_rectedge(rc, ji_color_facelight(), ji_color_faceshadow());
      jrect_shrink(rc, 1);

      jdraw_rect(rc, ji_color_face());
      jrect_shrink(rc, 1);

      /* status bar text */
      if (widget->text) {
	jdraw_rectfill(rc, ji_color_face());

	textout_ex(ji_screen, widget->text_font, widget->text,
		   rc->x1+2,
		   (widget->rc->y1+widget->rc->y2)/2-text_height(widget->text_font)/2,
		   ji_color_foreground(), -1);
      }

      /* draw progress bar */
      if (!jlist_empty(statusbar->progress)) {
	int width = 64;
	int y1, y2;
	int x = rc->x2 - (width+4);
	JLink link;

	y1 = rc->y1;
	y2 = rc->y2-1;

	JI_LIST_FOR_EACH(statusbar->progress, link) {
	  Progress *progress = link->data;
	  int u = (int)((float)(width-2)*progress->pos);
	  u = MID(0, u, width-2);

	  rect(ji_screen, x, y1, x+width-1, y2, ji_color_foreground());

	  if (u > 0)
	    rectfill(ji_screen, x+1, y1+1, x+u, y2-1, ji_color_selected());

	  if (1+u < width-2)
	    rectfill(ji_screen, x+1+u, y1+1, x+width-2, y2-1, ji_color_background());

	  x -= width+4;
	}
      }
      /* draw current sprite size in memory */
      else if (current_sprite != NULL) {
	char buf[1024];

	ustrcpy(buf, "Sprite:");
	get_pretty_memsize(sprite_get_memsize(current_sprite),
			   buf+ustrsize(buf),
			   sizeof(buf)-ustrsize(buf));

	ustrcat(buf, " Undo:");
	get_pretty_memsize(undo_get_memsize(current_sprite->undo),
			   buf+ustrsize(buf),
			   sizeof(buf)-ustrsize(buf));

	textout_right_ex(ji_screen, widget->text_font, buf,
			 rc->x2-2,
			 (widget->rc->y1+widget->rc->y2)/2-text_height(widget->text_font)/2,
			 ji_color_foreground(), -1);
      }

      jrect_free(rc);
      return TRUE;
    }

    case JM_MOUSEENTER:
      if (!jwidget_has_child(widget, statusbar->commands_box)) {
	Sprite *sprite = current_sprite;

	if (!sprite) {
	  jwidget_disable(statusbar->b_first);
	  jwidget_disable(statusbar->b_prev);
	  jwidget_disable(statusbar->b_play);
	  jwidget_disable(statusbar->b_next);
	  jwidget_disable(statusbar->b_last);
	}
	else {
	  jwidget_enable(statusbar->b_first);
	  jwidget_enable(statusbar->b_prev);
	  jwidget_enable(statusbar->b_play);
	  jwidget_enable(statusbar->b_next);
	  jwidget_enable(statusbar->b_last);
	}

	update_from_layer(statusbar);

	jwidget_add_child(widget, statusbar->commands_box);
	jwidget_dirty(widget);
      }
      break;

    case JM_MOUSELEAVE:
      if (jwidget_has_child(widget, statusbar->commands_box)) {
	/* if we want restore the state-bar and the slider doesn't have
	   the capture... */
	if (jmanager_get_capture() != statusbar->slider) {
	  /* exit from command mode */
	  jmanager_free_focus();

	  jwidget_remove_child(widget, statusbar->commands_box);
	  jwidget_dirty(widget);
	}
      }
      break;
  }

  return FALSE;
}

static bool tipwindow_msg_proc(JWidget widget, JMessage msg)
{
  switch (msg->type) {

    case JM_DESTROY:
      jmanager_remove_timer((int)widget->user_data[0]);
      break;

    case JM_TIMER:
      jwindow_close(widget, NULL);
      break;
  }

  return FALSE;
}

static bool slider_change_hook(JWidget widget, void *data)
{
  Sprite *sprite = current_sprite;

  if (sprite) {
    if ((sprite->layer) &&
	(sprite->layer->gfxobj.type == GFXOBJ_LAYER_IMAGE)) {
      Cel *cel = layer_get_cel(sprite->layer, sprite->frame);

      if (cel) {
	/* update the opacity */
	cel->opacity = jslider_get_value(widget);

	/* update the editors */
	update_screen_for_sprite(sprite);
      }
    }
  }

  return FALSE;
}

static void button_command(JWidget widget, void *data)
{
  Sprite *sprite = current_sprite;

  if (sprite) {
    const char *cmd = NULL;

    switch ((int)data) {

      case ACTION_LAYER:
	cmd = CMD_LAYER_PROPERTIES;
	break;

      case ACTION_FIRST:
	cmd = CMD_GOTO_FIRST_FRAME;
	break;

      case ACTION_PREV:
	cmd = CMD_GOTO_PREVIOUS_FRAME;
	break;

      case ACTION_PLAY:
	cmd = CMD_PLAY_ANIMATION;
	break;

      case ACTION_NEXT:
	cmd = CMD_GOTO_NEXT_FRAME;
	break;

      case ACTION_LAST:
	cmd = CMD_GOTO_LAST_FRAME;
	break;
    }

    if (cmd)
      command_execute(command_get_by_name(cmd), NULL);
  }
}

static void update_from_layer(StatusBar *statusbar)
{
  Sprite *sprite = current_sprite;
  Cel *cel;

  /* layer button */
  if (sprite && sprite->layer) {
    char buf[512];
    usprintf(buf, "[%d] %s", sprite->frame, sprite->layer->name);
    jwidget_set_text(statusbar->b_layer, buf);
    jwidget_enable(statusbar->b_layer);
  }
  else {
    jwidget_set_text(statusbar->b_layer, "Nothing");
    jwidget_disable(statusbar->b_layer);
  }

  /* opacity layer */
  if (sprite &&
      sprite->layer &&
      layer_is_image(sprite->layer) &&
      !layer_is_background(sprite->layer) &&
      (cel = layer_get_cel(sprite->layer, sprite->frame))) {
    jslider_set_value(statusbar->slider, MID(0, cel->opacity, 255));
    jwidget_enable(statusbar->slider);
  }
  else {
    jslider_set_value(statusbar->slider, 255);
    jwidget_disable(statusbar->slider);
  }
}
