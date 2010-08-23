/* ASE - Allegro Sprite Editor
 * Copyright (C) 2001-2010  David Capello
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

#include "jinete/jbox.h"
#include "jinete/jbutton.h"
#include "jinete/jhook.h"
#include "jinete/jlabel.h"
#include "jinete/jslider.h"
#include "jinete/jwidget.h"
#include "jinete/jwindow.h"
#include "Vaca/Bind.h"

#include "app.h"
#include "core/cfg.h"
#include "core/color.h"
#include "core/core.h"
#include "modules/editors.h"
#include "modules/gui.h"
#include "raster/image.h"
#include "raster/mask.h"
#include "raster/sprite.h"
#include "raster/undo.h"
#include "util/misc.h"
#include "widgets/color_bar.h"
#include "widgets/color_button.h"

static ColorButton* button_color;
static CheckBox* check_preview;
static Widget* slider_fuzziness; // TODO rename to tolerance

static void button_1_command(JWidget widget);
static void button_2_command(JWidget widget);
static bool color_change_hook(JWidget widget, void *data);
static bool slider_change_hook(JWidget widget, void *data);
static void preview_change_hook(Sprite* data);

static Mask *gen_mask(const Sprite* sprite);
static void mask_preview(Sprite* sprite);

void dialogs_mask_color(Sprite* sprite)
{
  JWidget box1, box2, box3, box4;
  Widget* label_color;
  Button* button_1;
  Button* button_2;
  JWidget label_fuzziness;
  Button* button_ok;
  Button* button_cancel;
  Image *image;

  if (!is_interactive () || !sprite)
    return;

  image = sprite->getCurrentImage();
  if (!image)
    return;

  FramePtr window(new Frame(false, _("Mask by Color")));
  box1 = jbox_new(JI_VERTICAL);
  box2 = jbox_new(JI_HORIZONTAL);
  box3 = jbox_new(JI_HORIZONTAL);
  box4 = jbox_new(JI_HORIZONTAL | JI_HOMOGENEOUS);
  label_color = new Label(_("Color:"));
  button_color = new ColorButton
   (get_config_color("MaskColor", "Color",
		     app_get_colorbar()->getFgColor()),
    sprite->getImgType());
  button_1 = new Button("1");
  button_2 = new Button("2");
  label_fuzziness = new Label(_("Fuzziness:"));
  slider_fuzziness = jslider_new(0, 255, get_config_int("MaskColor", "Fuzziness", 0));
  check_preview = new CheckBox(_("&Preview"));
  button_ok = new Button(_("&OK"));
  button_cancel = new Button(_("&Cancel"));

  if (get_config_bool("MaskColor", "Preview", true))
    check_preview->setSelected(true);

  button_1->user_data[1] = sprite;
  button_2->user_data[1] = sprite;

  button_1->Click.connect(Vaca::Bind<void>(&button_1_command, button_1));
  button_2->Click.connect(Vaca::Bind<void>(&button_2_command, button_2));
  button_ok->Click.connect(Vaca::Bind<void>(&Frame::closeWindow, window.get(), button_ok));
  button_cancel->Click.connect(Vaca::Bind<void>(&Frame::closeWindow, window.get(), button_cancel));

  HOOK(button_color, SIGNAL_COLORBUTTON_CHANGE, color_change_hook, sprite);
  HOOK(slider_fuzziness, JI_SIGNAL_SLIDER_CHANGE, slider_change_hook, sprite);
  check_preview->Click.connect(Vaca::Bind<void>(&preview_change_hook, sprite));

  jwidget_magnetic(button_ok, true);
  jwidget_expansive(button_color, true);
  jwidget_expansive(slider_fuzziness, true);
  jwidget_expansive(box2, true);

  jwidget_add_child(window, box1);
  jwidget_add_children(box1, box2, box3, check_preview, box4, NULL);
  jwidget_add_children(box2, label_color, button_color, button_1, button_2, NULL);
  jwidget_add_children(box3, label_fuzziness, slider_fuzziness, NULL);
  jwidget_add_children(box4, button_ok, button_cancel, NULL);

  /* default position */
  window->remap_window();
  window->center_window();

  /* mask first preview */
  mask_preview(sprite);

  /* load window configuration */
  load_window_pos(window, "MaskColor");

  /* open the window */
  window->open_window_fg();

  if (window->get_killer() == button_ok) {
    Mask *mask;

    /* undo */
    if (undo_is_enabled(sprite->getUndo())) {
      undo_set_label(sprite->getUndo(), "Mask by Color");
      undo_set_mask(sprite->getUndo(), sprite);
    }

    /* change the mask */
    mask = gen_mask(sprite);
    sprite->setMask(mask);
    mask_free(mask);

    set_config_color("MaskColor", "Color", button_color->getColor());
    set_config_int("MaskColor", "Fuzziness",
		   jslider_get_value(slider_fuzziness));
    set_config_bool("MaskColor", "Preview", check_preview->isSelected());
  }

  /* update boundaries and editors */
  sprite->generateMaskBoundaries();
  update_screen_for_sprite(sprite);

  /* save window configuration */
  save_window_pos(window, "MaskColor");
}

static void button_1_command(JWidget widget)
{
  button_color->setColor(app_get_colorbar()->getFgColor());
  mask_preview((Sprite*)widget->user_data[1]);
}

static void button_2_command(JWidget widget)
{
  button_color->setColor(app_get_colorbar()->getBgColor());
  mask_preview((Sprite*)widget->user_data[1]);
}

static bool color_change_hook(JWidget widget, void *data)
{
  mask_preview((Sprite*)data);
  return false;
}

static bool slider_change_hook(JWidget widget, void *data)
{
  mask_preview((Sprite*)data);
  return false;
}

static void preview_change_hook(Sprite* sprite)
{
  mask_preview(sprite);
}

static Mask *gen_mask(const Sprite* sprite)
{
  int xpos, ypos, color, fuzziness;

  const Image* image = sprite->getCurrentImage(&xpos, &ypos, NULL);

  color = get_color_for_image(sprite->getImgType(), button_color->getColor());
  fuzziness = jslider_get_value(slider_fuzziness);

  Mask* mask = mask_new();
  mask_by_color(mask, image, color, fuzziness);
  mask_move(mask, xpos, ypos);

  return mask;
}

static void mask_preview(Sprite* sprite)
{
  if (check_preview->isSelected()) {
    Mask* mask = gen_mask(sprite);

    sprite->generateMaskBoundaries(mask);
    update_screen_for_sprite(sprite);

    mask_free(mask);
  }
}
