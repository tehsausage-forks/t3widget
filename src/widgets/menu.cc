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
#include <ext/alloc_traits.h>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "t3widget/colorscheme.h"
#include "t3widget/dialogs/menupanel.h"
#include "t3widget/interfaces.h"
#include "t3widget/internal.h"
#include "t3widget/key.h"
#include "t3widget/mouse.h"
#include "t3widget/signals.h"
#include "t3widget/string_view.h"
#include "t3widget/util.h"
#include "t3widget/widgets/menu.h"
#include "t3widget/widgets/widget.h"
#include "t3window/window.h"

namespace t3widget {

struct menu_bar_t::implementation_t {
  // FIXME: the mutable keyword here is a hack to allow the hotkey operation to work. We should find
  // some way to get rid of this.
  mutable int
      current_menu, /**< Currently active window, when this menu_bar_t has the input focus. */
      old_menu;     /**< Previously active window. */
  bool hidden,      /**< Boolean indicating whether this menu_bar_t has "hidden" display type. */
      /** Boolean indicating whether this menu_bar_t (or rather one of its menus) has the input
         focus.
              See the comments at #set_focus for details.
      */
      has_focus;
  std::vector<std::unique_ptr<menu_panel_t>>
      menus;           /**< Vector of menus used for this menu_bar_t. */
  int button_down_idx; /** Index of menu on which the left button was pressed down. */
  signal_t<int> activate;

  implementation_t(bool _hidden)
      : current_menu(0), old_menu(0), hidden(_hidden), has_focus(false), button_down_idx(-1) {}
};

menu_bar_t::menu_bar_t(bool _hidden)
    : widget_t(1, 80, true, impl_alloc<implementation_t>(0)),
      impl(new_impl<implementation_t>(_hidden)) {
  // Menu bar should be above normal widgets
  window.set_depth(-1);
  if (impl->hidden) {
    window.hide();
  }
}

menu_bar_t::~menu_bar_t() {}

void menu_bar_t::draw_menu_name(const menu_panel_t &menu, bool selected) {
  int attr = selected ? attributes.menubar_selected : attributes.menubar;
  window.set_paint(0, menu.get_base_window()->get_x() + 1);
  window.addch(' ', attr);
  menu.draw_label(&window, attr, selected);
  window.addch(' ', attr);
}

void menu_bar_t::insert_menu(const menu_panel_t *before, std::unique_ptr<menu_panel_t> menu) {
  menu->set_menu_bar(this);

  std::vector<std::unique_ptr<menu_panel_t>>::iterator iter = impl->menus.end();
  if (before && before->get_menu_bar() == this) {
    for (iter = impl->menus.begin(); iter != impl->menus.end(); ++iter) {
      if (iter->get() == before) {
        break;
      }
    }
  }
  if (iter == impl->menus.end()) {
    before = nullptr;
  }

  int start_col;
  if (impl->menus.empty()) {
    start_col = 0;
  } else if (before) {
    start_col = before->get_base_window()->get_x();
    int next_start_col = start_col + menu->get_label_width() + 2;
    for (; iter != impl->menus.end(); ++iter) {
      (*iter)->set_position(None, next_start_col);
      next_start_col += (*iter)->get_label_width() + 2;
    }
  } else {
    start_col = (impl->menus.back()->get_base_window()->get_x() +
                 impl->menus.back()->get_label_width() + 2);
  }
  menu->set_position(None, start_col);
  menu->connect_activate(impl->activate.get_trigger());
  impl->menus.insert(iter, std::move(menu));
  force_redraw();
}

menu_panel_t *menu_bar_t::insert_menu(const menu_panel_t *before, string_view name) {
  menu_panel_t *result = new menu_panel_t(name);
  insert_menu(before, wrap_unique(result));
  return result;
}

std::unique_ptr<menu_panel_t> menu_bar_t::remove_menu(menu_panel_t *menu) {
  int idx = 0;
  for (auto iter = impl->menus.begin(); iter != impl->menus.end(); iter++, idx++) {
    if (iter->get() != menu) {
      continue;
    }

    menu->set_menu_bar(nullptr);

    if (impl->current_menu == idx) {
      if (impl->has_focus) {
        (*iter)->hide();
        next_menu();
        impl->menus[impl->current_menu]->show();
      }
    } else if (impl->current_menu > idx) {
      impl->current_menu--;
    }
    impl->old_menu = 0;  // Make sure impl->old_menu isn't out of bounds

    int start_col = (*iter)->get_base_window()->get_x();
    std::unique_ptr<menu_panel_t> result = std::move(*iter);

    iter = impl->menus.erase(iter);
    /* Move all the remaining impl->menus to their new position. */
    for (; iter != impl->menus.end(); iter++) {
      (*iter)->set_position(None, start_col);
      start_col += (*iter)->get_label_width() + 2;
    }
    force_redraw();
    return result;
  }
  return std::unique_ptr<menu_panel_t>();
}

void menu_bar_t::close() {
  impl->has_focus = false;
  if (impl->hidden) {
    window.hide();
  }
  draw_menu_name(*impl->menus[impl->current_menu], false);
  impl->menus[impl->current_menu]->hide();
  release_mouse_grab();
}

void menu_bar_t::next_menu() {
  impl->current_menu++;
  impl->current_menu %= impl->menus.size();
}

void menu_bar_t::previous_menu() {
  impl->current_menu += impl->menus.size() - 1;
  impl->current_menu %= impl->menus.size();
}

bool menu_bar_t::process_key(key_t key) {
  if (impl->menus.size() == 0) {
    return false;
  }

  switch (key) {
    case EKEY_HOTKEY:
      show();
      return true;
    default:
      return false;
  }
}

bool menu_bar_t::set_size(optint height, optint width) {
  (void)height;
  if (!width.is_valid()) {
    return true;
  }
  force_redraw();
  return window.resize(1, width.value()) == 0;
}

void menu_bar_t::update_contents() {
  if (reset_redraw()) {
    draw();
    if (impl->has_focus) {
      draw_menu_name(*impl->menus[impl->current_menu], true);
    }
    impl->old_menu = impl->current_menu;
  }

  if (!impl->has_focus) {
    return;
  }

  if (impl->old_menu == impl->current_menu) {
    impl->menus[impl->current_menu]->update_contents();
    return;
  }
  impl->menus[impl->old_menu]->hide();
  impl->menus[impl->current_menu]->show();
  draw_menu_name(*impl->menus[impl->old_menu], false);
  draw_menu_name(*impl->menus[impl->current_menu], true);
  impl->old_menu = impl->current_menu;
  impl->menus[impl->current_menu]->update_contents();
}

void menu_bar_t::set_focus(focus_t focus) { (void)focus; }

void menu_bar_t::show() {
  if (!impl->has_focus) {
    impl->has_focus = true;
    force_redraw();
    window.show();
    draw_menu_name(*impl->menus[impl->current_menu], true);
    impl->menus[impl->current_menu]->show();
    grab_mouse();
  }
}

void menu_bar_t::hide() {
  release_mouse_grab();
  widget_t::hide();
}

bool menu_bar_t::is_hotkey(key_t key) const {
  if (key == EKEY_F10 || key == '0') {
    impl->old_menu = impl->current_menu = 0;
    return true;
  }

  for (int i = 0; i < static_cast<int>(impl->menus.size()); i++) {
    if (impl->menus[i]->is_hotkey(key)) {
      impl->old_menu = impl->current_menu = i;
      return true;
    }
  }
  return false;
}

bool menu_bar_t::accepts_focus() const { return false; }

bool menu_bar_t::process_mouse_event(mouse_event_t event) {
  bool outside_area, on_bar;
  int current_menu_x;

  event.type &= ~EMOUSE_OUTSIDE_GRAB;

  if (event.y == 0) {
    outside_area = event.x < 0 || event.x >= window.get_width();
    on_bar = !outside_area;
  } else {
    int current_menu_width = impl->menus[impl->current_menu]->get_base_window()->get_width();
    int current_menu_height = impl->menus[impl->current_menu]->get_base_window()->get_height();
    current_menu_x = impl->menus[impl->current_menu]->get_base_window()->get_x();

    outside_area = event.x < current_menu_x || event.x >= current_menu_x + current_menu_width ||
                   event.y > current_menu_height || event.y < 0;
    on_bar = false;
  }

  if (on_bar) {
    if ((event.type == EMOUSE_BUTTON_PRESS || event.type == EMOUSE_MOTION) &&
        (event.button_state & EMOUSE_BUTTON_LEFT)) {
      int clicked_idx = coord_to_menu_idx(event.x);
      if (event.y == 0) {
        if (clicked_idx != -1) {
          impl->current_menu = clicked_idx;
          show();
        }
      }
    }
  } else if (outside_area) {
    if (event.type == EMOUSE_BUTTON_RELEASE) {
      close();
      return true;
    }
  } else {
    event.x -= current_menu_x;
    event.y -= 1;
    impl->menus[impl->current_menu]->process_mouse_event(event);
    return true;
  }
  return true;
}

int menu_bar_t::coord_to_menu_idx(int x) const {
  int idx = 0;
  for (auto iter = impl->menus.begin(); iter != impl->menus.end(); iter++, idx++) {
    int menu_start = (*iter)->get_base_window()->get_x() + 2;
    if (x < menu_start) {
      return -1;
    }
    if (x < menu_start + (*iter)->get_label_width()) {
      return idx;
    }
  }
  return -1;
}

void menu_bar_t::draw() {
  reset_redraw();
  window.set_paint(0, 0);
  window.addchrep(' ', attributes.menubar, window.get_width());
  for (const std::unique_ptr<menu_panel_t> &menu : impl->menus) {
    draw_menu_name(*menu, false);
  }
}

void menu_bar_t::set_hidden(bool _hidden) {
  impl->hidden = _hidden;
  if (impl->hidden) {
    window.hide();
  } else {
    window.show();
  }
}

_T3_WIDGET_IMPL_SIGNAL(menu_bar_t, activate, int)

}  // namespace t3widget
