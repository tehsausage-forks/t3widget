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
#include "t3widget/widgets/filepane.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

#include "t3widget/colorscheme.h"
#include "t3widget/contentlist.h"
#include "t3widget/dialogs/popup.h"
#include "t3widget/string_view.h"
#include "t3widget/textline.h"
#include "t3widget/widgets/scrollbar.h"
#include "t3widget/widgets/textfield.h"
#include "t3widget/widgets/widget.h"
#include "t3window/terminal.h"
#include "t3window/window.h"

namespace t3widget {

class T3_WIDGET_LOCAL file_pane_t::search_panel_t : public popup_t {
 private:
  file_pane_t *parent;
  bool redraw;
  text_line_t text;

 public:
  search_panel_t(file_pane_t *_parent);
  bool process_key(key_t key) override;
  void set_position(optint top, optint left) override;
  bool set_size(optint height, optint width) override;
  void update_contents() override;
  void show() override;
  bool process_mouse_event(mouse_event_t event) override;
};

struct file_pane_t::implementation_t {
  scrollbar_t scrollbar;       /**< Scrollbar displayed at the bottom. */
  size_t top_idx,              /**< Index of the first item displayed. */
      current;                 /**< Index of the currently highlighted item. */
  file_list_base_t *file_list; /**< List of files to display. */
  bool focus;          /**< Boolean indicating whether this file_pane_t has the input focus. */
  text_field_t *field; /**< The text_field_t which is the alternative input method for providing a
                          file name. */
  int column_widths[_T3_WDIGET_FP_MAX_COLUMNS],    /**< Width in cells of the various columns. */
      column_positions[_T3_WDIGET_FP_MAX_COLUMNS], /**< Left-most position for each column. */
      columns_visible, /**< The number of columns that are visible currently. */
      scrollbar_range; /**< Visible range for scrollbar setting. */
  connection_t
      content_changed_connection; /**< Connection to #file_list's content_changed signal. */
  std::unique_ptr<search_panel_t> search_panel;
  signal_t<const std::string &> activate;

  implementation_t()
      : scrollbar(false),
        top_idx(0),
        current(0),
        file_list(nullptr),
        focus(false),
        field(nullptr),
        columns_visible(0),
        scrollbar_range(1) {}
};

// FIXME: we could use some optimization for update_column_widths. Current use is simple but calls
// to often.
/* FIXME: we should not distribute left-over space among shown columns, but show partial column
   instead
        this is more intuitive for the user. */
file_pane_t::file_pane_t()
    : widget_t(3, 3, true, impl_alloc<implementation_t>(0)), impl(new_impl<implementation_t>()) {
  container_t::set_widget_parent(&impl->scrollbar);
  impl->scrollbar.set_anchor(this,
                             T3_PARENT(T3_ANCHOR_BOTTOMLEFT) | T3_CHILD(T3_ANCHOR_BOTTOMLEFT));
  impl->scrollbar.connect_clicked(bind_front(&file_pane_t::scrollbar_clicked, this));
  impl->scrollbar.connect_dragged(bind_front(&file_pane_t::scrollbar_dragged, this));
  impl->search_panel.reset(new search_panel_t(this));
}

file_pane_t::~file_pane_t() { impl->content_changed_connection.disconnect(); }

void file_pane_t::set_text_field(text_field_t *_field) { impl->field = _field; }

void file_pane_t::ensure_cursor_on_screen() {
  size_t old_top_idx = impl->top_idx;
  int height;

  if (impl->file_list == nullptr) {
    return;
  }

  height = window.get_height() - 1;

  while (impl->current >= impl->top_idx + impl->columns_visible * height) {
    impl->top_idx += height;
  }
  while (impl->current < impl->top_idx && impl->top_idx > static_cast<size_t>(height)) {
    impl->top_idx -= height;
  }
  if (impl->top_idx > impl->current) {
    impl->top_idx = 0;
  }
  if (impl->top_idx != old_top_idx) {
    update_column_widths();
    ensure_cursor_on_screen();
  }
}

bool file_pane_t::process_key(key_t key) {
  int height;
  if (impl->file_list == nullptr) {
    return false;
  }

  switch (key) {
    case EKEY_DOWN:
      if (impl->current + 1 >= impl->file_list->size()) {
        return true;
      }
      impl->current++;
      force_redraw();
      break;
    case EKEY_UP:
      if (impl->current == 0) {
        return true;
      }
      impl->current--;
      force_redraw();
      break;
    case EKEY_RIGHT:
      height = window.get_height() - 1;
      if (impl->current + height >= impl->file_list->size()) {
        if (impl->file_list->size() == 0) {
          return true;
        }
        impl->current = impl->file_list->size() - 1;
      } else {
        impl->current += height;
      }
      force_redraw();
      break;
    case EKEY_LEFT:
      height = window.get_height() - 1;
      if (impl->current < static_cast<size_t>(height)) {
        impl->current = 0;
      } else {
        impl->current -= height;
      }
      force_redraw();
      break;
    case EKEY_END:
      impl->current = impl->file_list->size() - 1;
      force_redraw();
      break;
    case EKEY_HOME:
      impl->current = 0;
      force_redraw();
      break;
    case EKEY_PGDN:
      height = window.get_height() - 1;
      if (impl->current + 2 * height >= impl->file_list->size()) {
        impl->current = impl->file_list->size() - 1;
      } else {
        impl->current += 2 * height;
        impl->top_idx += 2 * height;
      }
      force_redraw();
      break;
    case EKEY_PGUP:
      height = window.get_height() - 1;
      if (impl->current < static_cast<size_t>(2) * height) {
        impl->current = 0;
      } else {
        impl->current -= 2 * height;
        if (impl->top_idx < static_cast<size_t>(2) * height) {
          impl->top_idx = 0;
        } else {
          impl->top_idx -= 2 * height;
        }
      }
      force_redraw();
      break;
    case EKEY_NL:
      impl->activate(impl->file_list->get_fs_name(impl->current));
      return true;
    default:
      if (key >= 32 && key < EKEY_FIRST_SPECIAL) {
        impl->search_panel->show();
        impl->search_panel->process_key(key);
        return true;
      }
      return false;
  }
  if (impl->file_list->size() != 0) {
    if (impl->field != nullptr) {
      impl->field->set_text((*impl->file_list)[impl->current]);
    }
    ensure_cursor_on_screen();
  }
  return true;
}

bool file_pane_t::set_size(optint height, optint width) {
  bool result;
  if (!height.is_valid()) {
    height = window.get_height();
  }
  if (!width.is_valid()) {
    width = window.get_height();
  }
  result = window.resize(height.value(), width.value());
  result &= impl->scrollbar.set_size(None, width);

  height = height.value() - 1;
  if (impl->file_list != nullptr && impl->file_list->size() != 0) {
    update_column_widths();
    impl->scrollbar_range =
        ((impl->file_list->size() + height.value() - 1) / height.value()) * height.value();
    ensure_cursor_on_screen();
  }
  force_redraw();
  return result;
}

void file_pane_t::draw_line(int idx, bool selected) {
  if (static_cast<size_t>(idx) < impl->top_idx ||
      static_cast<size_t>(idx) >= impl->file_list->size()) {
    return;
  }

  int column;
  int height = window.get_height() - 1;
  text_line_t line((*impl->file_list)[idx]);
  bool is_dir = impl->file_list->is_dir(idx);
  text_line_t::paint_info_t info;

  idx -= impl->top_idx;
  column = idx / height;
  idx %= height;

  window.set_paint(idx, impl->column_positions[column]);
  window.addch(is_dir ? '/' : ' ', selected ? attributes.dialog_selected : 0);

  info.start = 0;
  info.leftcol = 0;
  info.max = std::numeric_limits<text_pos_t>::max();
  info.size = impl->column_widths[column];
  info.tabsize = 0;
  info.flags = text_line_t::SPACECLEAR | text_line_t::TAB_AS_CONTROL |
               (selected ? text_line_t::EXTEND_SELECTION : 0);
  info.selection_start = -1;
  info.selection_end = selected ? std::numeric_limits<text_pos_t>::max() : -1;
  info.cursor = -1;
  info.normal_attr = attributes.dialog;
  info.selected_attr = attributes.dialog_selected;

  line.paint_line(&window, info);
}

void file_pane_t::update_contents() {
  size_t max_idx, i;
  int height;

  impl->search_panel->update_contents();

  if (!reset_redraw()) {
    return;
  }

  window.set_default_attrs(attributes.dialog);

  window.set_paint(0, 0);
  window.clrtobot();

  if (impl->file_list == nullptr) {
    return;
  }

  height = window.get_height() - 1;
  for (i = impl->top_idx,
      max_idx = std::min(impl->top_idx + impl->columns_visible * height, impl->file_list->size());
       i < max_idx; i++) {
    draw_line(i, impl->focus && i == impl->current);
  }

  impl->scrollbar.set_parameters(impl->scrollbar_range, impl->top_idx,
                                 impl->columns_visible * height);
  impl->scrollbar.update_contents();
}

void file_pane_t::set_focus(focus_t _focus) {
  impl->focus = _focus;
  if (impl->file_list != nullptr) {
    draw_line(impl->current, impl->focus);
  }
}

void file_pane_t::set_child_focus(window_component_t *target) {
  (void)target;
  set_focus(window_component_t::FOCUS_SET);
}

bool file_pane_t::is_child(const window_component_t *widget) const {
  return widget == &impl->scrollbar;
}

bool file_pane_t::process_mouse_event(mouse_event_t event) {
  if (event.window != window) {
    return true;
  }
  if ((event.type == EMOUSE_BUTTON_RELEASE && (event.button_state & EMOUSE_DOUBLE_CLICKED_LEFT)) ||
      (event.type == EMOUSE_BUTTON_PRESS && (event.button_state & EMOUSE_BUTTON_LEFT))) {
    int column;
    size_t idx;

    if ((event.button_state & (EMOUSE_BUTTON_LEFT | EMOUSE_DOUBLE_CLICKED_LEFT)) == 0 ||
        impl->columns_visible == 0) {
      return true;
    }

    for (column = 1; column < impl->columns_visible && impl->column_positions[column] < event.x;
         column++) {
    }
    column--;
    idx = column * (window.get_height() - 1) + event.y + impl->top_idx;
    if (idx > impl->file_list->size()) {
      return true;
    }
    if (event.button_state & EMOUSE_DOUBLE_CLICKED_LEFT) {
      impl->activate(impl->file_list->get_fs_name(impl->current));
    } else if (event.button_state & EMOUSE_BUTTON_LEFT) {
      impl->current = idx;
      if (impl->field != nullptr) {
        impl->field->set_text((*impl->file_list)[impl->current]);
      }
      force_redraw();
      return true;
    }
  }
  return true;
}

void file_pane_t::reset() {
  impl->top_idx = 0;
  impl->current = 0;
}

void file_pane_t::set_file_list(file_list_base_t *_file_list) {
  if (impl->file_list != nullptr) {
    impl->content_changed_connection.disconnect();
  }

  impl->file_list = _file_list;
  impl->content_changed_connection =
      impl->file_list->connect_content_changed([this] { content_changed(); });
  impl->top_idx = 0;
  content_changed();
  force_redraw();
}

void file_pane_t::set_file(const std::string &name) {
  for (impl->current = 0; impl->current < impl->file_list->size(); impl->current++) {
    if (name.compare((*impl->file_list)[impl->current]) == 0) {
      break;
    }
  }
  if (impl->current == impl->file_list->size()) {
    impl->current = 0;
  }

  ensure_cursor_on_screen();
}

void file_pane_t::update_column_width(int column, int start) {
  int height = window.get_height() - 1;
  impl->column_widths[column] = 0;
  for (int i = 0; i < height && start + i < static_cast<int>(impl->file_list->size()); i++) {
    const std::string &item = (*impl->file_list)[i + start];
    impl->column_widths[column] =
        std::max<int>(impl->column_widths[column], t3_term_strncwidth(item.data(), item.size()));
  }
}

void file_pane_t::update_column_widths() {
  int i, sum_width;
  int height = window.get_height() - 1;
  int width = window.get_width();

  if (impl->file_list == nullptr) {
    return;
  }

  for (i = 0, sum_width = 0; i < _T3_WDIGET_FP_MAX_COLUMNS && sum_width < width &&
                             impl->top_idx + i * height < impl->file_list->size();
       i++) {
    update_column_width(i, impl->top_idx + i * height);
    sum_width += impl->column_widths[i] + 2;
  }

  impl->columns_visible = i;
  if (sum_width > width) {
    if (i > 1) {
      sum_width -= impl->column_widths[i - 1] + 2;
      impl->columns_visible = i - 1;
    } else {
      impl->column_widths[0] = width;
      sum_width = width;
    }
  }
  if (impl->columns_visible == 0) {
    impl->columns_visible = 1;
  }

  for (i = 0; i < impl->columns_visible; i++) {
    impl->column_widths[i] += (width - sum_width) / impl->columns_visible;
  }
  // sum_width += impl->columns_visible * ((width - sum_width) / impl->columns_visible);
  for (i = 0; i < impl->columns_visible; i++) {
    impl->column_widths[i]++;
  }
  impl->column_positions[0] = 0;
  for (i = 1; i < impl->columns_visible; i++) {
    impl->column_positions[i] = impl->column_positions[i - 1] + impl->column_widths[i - 1] + 1;
  }
}

void file_pane_t::content_changed() {
  int height = window.get_height() - 1;

  impl->top_idx = 0;
  update_column_widths();
  impl->scrollbar_range = ((impl->file_list->size() + height - 1) / height) * height;
  force_redraw();
}

void file_pane_t::scrollbar_clicked(scrollbar_t::step_t step) {
  int height = window.get_height() - 1;
  if (impl->file_list == nullptr) {
    return;
  }

  /* FIXME: for now, clicking the empty area of the scrollbar will simply
          jump one column, because doing it differently is too tricky (for now). */
  if (step == scrollbar_t::FWD_SMALL || step == scrollbar_t::FWD_MEDIUM ||
      step == scrollbar_t::FWD_PAGE) {
    if (impl->top_idx + impl->columns_visible * height >= impl->file_list->size()) {
      return;
    }
    impl->top_idx += height;
  } else if (step == scrollbar_t::FWD_PAGE) {
    if (impl->current + 2 * height >= impl->file_list->size()) {
      impl->current = impl->file_list->size() - 1;
    } else {
      impl->current += 2 * height;
      impl->top_idx += 2 * height;
    }
  } else if (step == scrollbar_t::BACK_SMALL || step == scrollbar_t::BACK_MEDIUM ||
             step == scrollbar_t::BACK_PAGE) {
    if (impl->top_idx == 0) {
      return;
    }
    if (impl->top_idx < static_cast<size_t>(height)) {
      impl->top_idx = 0;
    } else {
      impl->top_idx -= height;
    }
  } else if (step == scrollbar_t::BACK_PAGE) {
    if (impl->current < static_cast<size_t>(2) * height) {
      impl->current = 0;
    } else {
      impl->current -= 2 * height;
      if (impl->top_idx < static_cast<size_t>(2) * height) {
        impl->top_idx = 0;
      } else {
        impl->top_idx -= 2 * height;
      }
    }
  }

  update_column_widths();
  if (impl->current < impl->top_idx) {
    impl->current = impl->top_idx;
  } else if (impl->current >= impl->file_list->size()) {
    impl->current = impl->file_list->size() - 1;
  } else if (impl->current >= impl->top_idx + impl->columns_visible * height) {
    impl->current = impl->top_idx + impl->columns_visible * height - 1;
  }
  force_redraw();

  if (impl->file_list->size() != 0 && impl->field != nullptr) {
    impl->field->set_text((*impl->file_list)[impl->current]);
  }
}

void file_pane_t::scrollbar_dragged(text_pos_t start) {
  int height = window.get_height() - 1;
  size_t new_top_idx = (start / height) * height;
  if (start >= 0 && new_top_idx < impl->file_list->size() && new_top_idx != impl->top_idx) {
    impl->top_idx = new_top_idx;
    force_redraw();
  }
}

void file_pane_t::search(const std::string &text) {
  size_t i, j;
  size_t longest_match = 0;
  size_t longest_match_idx = 0;

  for (i = 0; i < impl->file_list->size(); i++) {
    const std::string &item = (*impl->file_list)[i];
    for (j = 0; j < item.size() && j < text.size(); j++) {
      if (item[j] != text[j]) {
        break;
      }
    }
    // Adjust match length to start of UTF-8 character.
    while (j > 0 && (text[j] & 0xC0) == 0x80) {
      j--;
    }
    if (j > longest_match) {
      longest_match = j;
      longest_match_idx = i;
    }
  }
  if (longest_match > 0 && impl->current != longest_match_idx) {
    impl->current = longest_match_idx;
    force_redraw();
    ensure_cursor_on_screen();
  }
}

//============================ search_panel_t ==========================
#define SEARCH_PANEL_WIDTH 12
file_pane_t::search_panel_t::search_panel_t(file_pane_t *_parent)
    : popup_t(3, SEARCH_PANEL_WIDTH), parent(_parent), redraw(false) {
  window.set_anchor(parent->get_base_window(),
                    T3_PARENT(T3_ANCHOR_BOTTOMRIGHT) | T3_CHILD(T3_ANCHOR_TOPRIGHT));
  window.move(-1, 1);
}

bool file_pane_t::search_panel_t::process_key(key_t key) {
  switch (key) {
    case EKEY_BS:
      text.backspace_char(text.size(), nullptr);
      redraw = true;
      return true;
    case EKEY_ESC:
      hide();
      return true;
    default:
      if (key < 32) {
        break;
      }

      key &= ~EKEY_PROTECT;
      if (key == 10) {
        break;
      }

      if (key >= EKEY_FIRST_SPECIAL) {
        break;
      }
      text.append_char(key, nullptr);
      parent->search(text.get_data());
      redraw = true;
      return true;
  }
  hide();
  return popup_t::process_key(key);
}

void file_pane_t::search_panel_t::set_position(optint top, optint left) {
  (void)top;
  (void)left;
}

bool file_pane_t::search_panel_t::set_size(optint height, optint width) {
  (void)height;
  (void)width;
  return true;
}

void file_pane_t::search_panel_t::update_contents() {
  if (!redraw) {
    return;
  }

  popup_t::update_contents();

  text_line_t::paint_info_t paint_info;
  text_pos_t text_width = text.calculate_screen_width(0, std::numeric_limits<text_pos_t>::max(), 0);

  paint_info.start = 0;
  paint_info.leftcol =
      SEARCH_PANEL_WIDTH - 2 < text_width ? text_width - SEARCH_PANEL_WIDTH + 2 : 0;
  paint_info.size = SEARCH_PANEL_WIDTH - 2;
  paint_info.max = std::numeric_limits<text_pos_t>::max();
  paint_info.tabsize = 0;
  paint_info.flags = text_line_t::TAB_AS_CONTROL | text_line_t::SPACECLEAR;
  paint_info.selection_start = -1;
  paint_info.selection_end = -1;
  paint_info.cursor = -1;
  paint_info.normal_attr = 0;
  paint_info.selected_attr = 0;

  window.set_paint(1, 1);
  text.paint_line(&window, paint_info);
}

void file_pane_t::search_panel_t::show() {
  popup_t::show();
  text.set_text("");
}

bool file_pane_t::search_panel_t::process_mouse_event(mouse_event_t event) {
  if ((event.type & EMOUSE_OUTSIDE_GRAB) &&
      (event.type & ~EMOUSE_OUTSIDE_GRAB) == EMOUSE_BUTTON_PRESS) {
    hide();
    return false;
  }
  return true;
}

connection_t file_pane_t::connect_activate(std::function<void(const std::string &)> cb) {
  return impl->activate.connect(cb);
}

}  // namespace t3widget
