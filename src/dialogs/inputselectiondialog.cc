/* Copyright (C) 2011,2013,2018 G.P. Halkes
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
#include <cstring>
#include <memory>
#include <string>
#include <t3window/utf8.h>
#include <type_traits>
#include <utility>

#include "t3widget/dialogs/dialog.h"
#include "t3widget/dialogs/inputselectiondialog.h"
#include "t3widget/internal.h"
#include "t3widget/key.h"
#include "t3widget/main.h"
#include "t3widget/signals.h"
#include "t3widget/string_view.h"
#include "t3widget/textbuffer.h"
#include "t3widget/util.h"
#include "t3widget/widgets/button.h"
#include "t3widget/widgets/checkbox.h"
#include "t3widget/widgets/frame.h"
#include "t3widget/widgets/label.h"
#include "t3widget/widgets/smartlabel.h"
#include "t3widget/widgets/textwindow.h"
#include "t3window/window.h"

namespace t3widget {

struct input_selection_dialog_t::implementation_t {
  std::unique_ptr<text_buffer_t> text;
  frame_t *text_frame, *label_frame;
  text_window_t *text_window;
  label_t *key_label;
  checkbox_t *enable_simulate_box, *disable_timeout_box;
  int old_timeout;
  signal_t<> activate;
};

input_selection_dialog_t::input_selection_dialog_t(int height, int width,
                                                   std::unique_ptr<text_buffer_t> _text)
    : dialog_t(height, width, _("Input Handling"), impl_alloc<implementation_t>(0)),
      impl(new_impl<implementation_t>()) {
  impl->text = std::move(_text);
  if (!impl->text) {
    impl->text = get_default_text();
  }

  impl->text_frame = emplace_back<frame_t>(frame_t::COVER_RIGHT);
  impl->text_frame->set_size(height - 9, width - 2);
  impl->text_frame->set_position(1, 1);

  impl->text_window = impl->text_frame->emplace_child<text_window_t>(impl->text.get());

  impl->label_frame = emplace_back<frame_t>();
  impl->label_frame->set_anchor(impl->text_frame,
                                T3_PARENT(T3_ANCHOR_BOTTOMCENTER) | T3_CHILD(T3_ANCHOR_TOPCENTER));
  impl->label_frame->set_position(0, 0);
  impl->label_frame->set_size(3, 18);

  impl->key_label = impl->label_frame->emplace_child<label_t>("");
  impl->key_label->set_accepts_focus(false);
  impl->key_label->set_align(label_t::ALIGN_CENTER);

  impl->enable_simulate_box = emplace_back<checkbox_t>();
  impl->enable_simulate_box->set_anchor(
      this, T3_PARENT(T3_ANCHOR_BOTTOMLEFT) | T3_PARENT(T3_ANCHOR_BOTTOMLEFT));
  impl->enable_simulate_box->set_position(-5, 2);
  impl->enable_simulate_box->connect_toggled([this] { check_state(); });
  impl->enable_simulate_box->connect_activate([this] { ok_activated(); });
  impl->enable_simulate_box->connect_move_focus_up([this] { focus_previous(); });
  impl->enable_simulate_box->connect_move_focus_down([this] { focus_next(); });

  smart_label_t *enable_simulate_label =
      emplace_back<smart_label_t>("'Esc <letter>' simulates Meta+<letter>");
  enable_simulate_label->set_anchor(impl->enable_simulate_box,
                                    T3_PARENT(T3_ANCHOR_TOPRIGHT) | T3_CHILD(T3_ANCHOR_TOPLEFT));
  enable_simulate_label->set_position(0, 1);

  smart_label_t *close_remark_label =
      emplace_back<smart_label_t>("(Requires 'Esc Esc' to close menu or dialog)");
  close_remark_label->set_anchor(enable_simulate_label, 0);
  close_remark_label->set_position(1, 0);

  impl->disable_timeout_box = emplace_back<checkbox_t>();
  impl->disable_timeout_box->set_anchor(impl->enable_simulate_box, 0);
  impl->disable_timeout_box->set_position(2, 0);
  impl->disable_timeout_box->connect_activate([this] { ok_activated(); });
  impl->disable_timeout_box->connect_move_focus_up([this] { focus_previous(); });
  impl->disable_timeout_box->connect_move_focus_down([this] { focus_next(); });

  smart_label_t *disable_timeout_label = emplace_back<smart_label_t>("Disable timeout on Esc");
  disable_timeout_label->set_anchor(impl->disable_timeout_box,
                                    T3_PARENT(T3_ANCHOR_TOPRIGHT) | T3_CHILD(T3_ANCHOR_TOPLEFT));
  disable_timeout_label->set_position(0, 1);

  button_t *ok_button = emplace_back<button_t>(_("Ok"), true);
  button_t *cancel_button = emplace_back<button_t>(_("Cancel"));

  cancel_button->set_anchor(this,
                            T3_PARENT(T3_ANCHOR_BOTTOMRIGHT) | T3_CHILD(T3_ANCHOR_BOTTOMRIGHT));
  cancel_button->set_position(-1, -2);
  cancel_button->connect_activate([this] { cancel(); });
  cancel_button->connect_move_focus_left([this] { focus_previous(); });
  cancel_button->connect_move_focus_up([this] { focus_previous(); });
  cancel_button->connect_move_focus_up([this] { focus_previous(); });

  ok_button->set_anchor(cancel_button, T3_PARENT(T3_ANCHOR_TOPLEFT) | T3_CHILD(T3_ANCHOR_TOPRIGHT));
  ok_button->set_position(0, -2);
  ok_button->connect_activate([this] { ok_activated(); });
  ok_button->connect_move_focus_right([this] { focus_next(); });
  ok_button->connect_move_focus_up([this] { focus_previous(); });
}

input_selection_dialog_t::~input_selection_dialog_t() {}

bool input_selection_dialog_t::set_size(optint height, optint width) {
  bool result;

  if (!height.is_valid()) {
    height = window.get_height();
  }
  if (!width.is_valid()) {
    width = window.get_width();
  }

  result = dialog_t::set_size(height, width);
  result &= impl->text_frame->set_size(height.value() - 9, width.value() - 2);
  return result;
}

bool input_selection_dialog_t::process_key(key_t key) {
  switch (key) {
    case EKEY_ESC:
    case EKEY_ESC | EKEY_META:
      set_key_timeout(impl->old_timeout);
      close();
      return true;
    case '\t' | EKEY_META:
    case EKEY_RIGHT | EKEY_META:
    case EKEY_LEFT | EKEY_META:
      return dialog_t::process_key(key & ~EKEY_META);
    default:
      if ((key & ~EKEY_META) < EKEY_FIRST_SPECIAL && (key & ~EKEY_META) > 0x20) {
        char buffer[16];
        size_t buffer_contents_length = t3_utf8_put(key & ~EKEY_META, buffer);
        std::string result;

        if (key & EKEY_META) {
          result = _("Meta-");
        }
        result.append(buffer, buffer_contents_length);
        impl->key_label->set_text(result.c_str());
        return true;
      }
      if (!dialog_t::process_key(key)) {
        impl->key_label->set_text("<other>");
      }
      return true;
  }
}

void input_selection_dialog_t::show() {
  impl->old_timeout = get_key_timeout();
  set_key_timeout(-1000);
  if (impl->old_timeout <= 0) {
    impl->enable_simulate_box->set_state(true);
    impl->disable_timeout_box->set_state(impl->old_timeout == 0);
    impl->disable_timeout_box->set_enabled(true);
  } else {
    impl->enable_simulate_box->set_state(false);
    impl->disable_timeout_box->set_enabled(false);
  }
  dialog_t::show();
}

std::unique_ptr<text_buffer_t> input_selection_dialog_t::get_default_text() {
  std::unique_ptr<text_buffer_t> default_text = make_unique<text_buffer_t>();
  const char *intl_text =
      _("%s provides an intuitive interface for people accustomed "
        "to GUI applications. For example, it allows you to use Meta+<letter> combinations to open "
        "menus and jump to items on your screen. However, not all terminals and terminal emulators "
        "handle the Meta key the same way. The result is that %s can not reliably handle the "
        "Meta+<letter> combinations on all terminals. While this dialog is open, the box "
        "below will show which keys you pressed, allowing you to test whether the Meta "
        "key is fully functional.\n\n");

  const char *insert_point = strstr(intl_text, "%s");

  default_text->append_text(string_view(intl_text, insert_point - intl_text));
  default_text->append_text(init_params->program_name);
  intl_text = insert_point + 2;
  insert_point = strstr(intl_text, "%s");
  default_text->append_text(string_view(intl_text, insert_point - intl_text));
  default_text->append_text(init_params->program_name);
  default_text->append_text(insert_point + 2);

  intl_text =
      _("As an alternative to Meta+<letter>, %s can allow you to simulate "
        "Meta+<letter> by pressing Esc followed by <letter>. However, this does mean that you have "
        "to "
        "press Esc twice to close a menu or dialog. While this dialog is open, this work-around "
        "is enabled. If you do not require this work-around because Meta+<letter> is fully "
        "functional, "
        "you can disable it below for the rest of the program, allowing you to close menus and "
        "dialogs "
        "(except this one) with a single press of the Esc key.\n\n");
  insert_point = strstr(intl_text, "%s");
  default_text->append_text(string_view(intl_text, insert_point - intl_text));
  default_text->append_text(init_params->program_name);
  default_text->append_text(insert_point + 2);

  default_text->append_text(_(
      "When the 'Esc <letter>' work-around is enabled, the fact that you "
      "pressed the Esc key is discarded after one second. This may be inconvenient in some cases, "
      "therefore the timeout on the Esc key can be disabled.\n\n"));
  default_text->append_text(
      _("Other methods\n=============\n\nSome terminal emulators have configuration "
        "options to either use Meta+<letter> for their own purposes, or pass the key combination "
        "through to the "
        "program running in the terminal. An example of this is gnome-terminal. Furthermore, some "
        "terminal "
        "emulators only intercept Meta+<letter> but not Meta+Shift+<letter>. This combination is "
        "therefore "
        "also accepted as if it were Meta+<letter>."));

  return default_text;
}

void input_selection_dialog_t::cancel() {
  set_key_timeout(impl->old_timeout);
  close();
}

void input_selection_dialog_t::ok_activated() {
  hide();
  set_key_timeout(impl->enable_simulate_box->get_state()
                      ? (impl->disable_timeout_box->get_state() ? 0 : -1000)
                      : 100);
  impl->activate();
}

void input_selection_dialog_t::check_state() {
  impl->disable_timeout_box->set_enabled(impl->enable_simulate_box->get_state());
}

_T3_WIDGET_IMPL_SIGNAL(input_selection_dialog_t, activate)

}  // namespace t3widget
