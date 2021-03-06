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
#ifndef T3_WIDGET_SCROLLBAR_H
#define T3_WIDGET_SCROLLBAR_H

#include <t3widget/widgets/widget.h>

namespace t3widget {

class T3_WIDGET_API scrollbar_t : public widget_t {
 private:
  struct T3_WIDGET_LOCAL implementation_t;
  single_alloc_pimpl_t<implementation_t> impl;

 public:
  scrollbar_t(bool _vertical);
  ~scrollbar_t() override;
  bool process_key(key_t key) override;
  bool set_size(optint height, optint width) override;
  void update_contents() override;
  bool accepts_focus() const override;
  void set_focus(focus_t focus) override;
  bool process_mouse_event(mouse_event_t event) override;

  void set_parameters(text_pos_t _range, text_pos_t _start, int _used);

  enum step_t {
    FWD_SMALL,   /**< Mouse click on arrow symbol. */
    FWD_MEDIUM,  /**< Scroll wheel over bar. */
    FWD_PAGE,    /**< Mouse click on space between arrow and indicator. */
    BACK_SMALL,  /**< Mouse click on arrow symbol. */
    BACK_MEDIUM, /**< Scroll wheel over bar. */
    BACK_PAGE    /**< Mouse click on space between arrow and indicator. */
  };

  connection_t connect_clicked(std::function<void(step_t)> cb);
  connection_t connect_dragged(std::function<void(text_pos_t)> cb);
};

}  // namespace t3widget
#endif
