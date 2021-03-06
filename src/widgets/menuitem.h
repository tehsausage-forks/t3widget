/* Copyright (C) 2011-2013,2018 G.P. Halkes
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 3, as
   published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef T3_WIDGET_MENUITEM_H
#define T3_WIDGET_MENUITEM_H

#include <cstddef>
#include <t3widget/dialogs/menupanel.h>
#include <t3widget/widgets/widget.h>

#include <t3widget/interfaces.h>
#include <t3widget/key.h>
#include <t3widget/mouse.h>
#include <t3widget/string_view.h>
#include <t3widget/util.h>
#include <t3widget/widget_api.h>

namespace t3widget {

class T3_WIDGET_API menu_item_base_t : public widget_t {
 protected:
  friend class menu_panel_t;
  menu_panel_t *parent;

  /* Menu items get their events from the menu_t (via the menu_panel_t),
     because that grabs the mouse as soon as it is activated. */
  virtual void process_mouse_event_from_menu(mouse_event_t event);

 public:
  T3_WIDGET_DISALLOW_COPY(menu_item_base_t)

  menu_item_base_t(menu_panel_t *_parent, size_t impl_size = 0)
      : widget_t(1, 4, true, impl_size), parent(_parent) {}
  void set_position(optint top, optint left) override;
  bool set_size(optint height, optint width) override;
};

class T3_WIDGET_API menu_item_t : public menu_item_base_t {
 private:
  struct T3_WIDGET_LOCAL implementation_t;

  single_alloc_pimpl_t<implementation_t> impl;

  /* Menu items get their events from the menu_t (via the menu_panel_t),
     because that grabs the mouse as soon as it is activated. */
  void process_mouse_event_from_menu(mouse_event_t event) override;

 public:
  menu_item_t(menu_panel_t *_parent, string_view _label, string_view _hotkey, int _id);
  ~menu_item_t() override;
  bool process_key(key_t key) override;
  void update_contents() override;
  void set_focus(focus_t focus) override;
  void show() override;
  void hide() override;
  bool is_hotkey(key_t key) const override;
  text_pos_t get_label_width() const;
  text_pos_t get_shortcut_key_width() const;
};

class T3_WIDGET_API menu_separator_t : public menu_item_base_t {
 public:
  menu_separator_t(menu_panel_t *_parent);
  bool process_key(key_t key) override;
  void update_contents() override;
  void set_focus(focus_t focus) override;
  void show() override;
  void hide() override;
  bool accepts_focus() const override;
};

}  // namespace t3widget
#endif
