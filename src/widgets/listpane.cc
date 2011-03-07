/* Copyright (C) 2010 G.P. Halkes
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
#include "colorscheme.h"
#include "widgets/listpane.h"

using namespace std;

list_pane_t::list_pane_t(container_t *_parent, bool _indicator) : height(1), top_idx(0), current(0), parent(_parent),
		widgets(NULL), has_focus(false), scrollbar(_parent, true), indicator(_indicator)
{
	/* We temporarily store the clip_window in window, so that we can use set_anchor on
	   the scrollbar. */
	if ((clip_window = window = t3_win_new_unbacked(parent->get_draw_window(), height, 3, 0, 0, 0)) == NULL)
		throw bad_alloc();
	scrollbar.set_anchor(this, T3_PARENT(T3_ANCHOR_TOPRIGHT) | T3_CHILD(T3_ANCHOR_TOPLEFT));
	scrollbar.set_size(height, None);

	if ((window = t3_win_new_unbacked(clip_window, 1, 3, 0, 0, 0)) == NULL)
		throw bad_alloc();

	t3_win_set_anchor(window, clip_window, T3_PARENT(T3_ANCHOR_TOPLEFT) | T3_CHILD(T3_ANCHOR_TOPLEFT));
	t3_win_set_default_attrs(window, colors.dialog_attrs);
	t3_win_show(window);
	t3_win_show(clip_window);

	if (indicator)
		indicator_widget = new indicator_widget_t(this);
}

list_pane_t::~list_pane_t(void) {
	t3_win_del(clip_window);
}

void list_pane_t::ensure_cursor_on_screen(void) {
	if (current >= top_idx + height)
		top_idx = current - height + 1;
	else if (current < top_idx)
		top_idx = current;
}

bool list_pane_t::process_key(key_t key) {
	size_t old_current = current;
	switch (key) {
		case EKEY_DOWN:
			if (current + 1 >= widgets.size())
				return true;
			current++;
			break;
		case EKEY_UP:
			if (current == 0)
				return true;
			current--;
			break;
		case EKEY_END:
			current = widgets.size() - 1;
			break;
		case EKEY_HOME:
			current = 0;
			break;
		case EKEY_PGDN:
			if (current + height >= widgets.size()) {
				current = widgets.size() - 1;
			} else {
				current += height;
				if (top_idx + 2 * height < widgets.size())
					top_idx += height;
				else
					top_idx = widgets.size() - height;
			}
			break;
		case EKEY_PGUP:
			if (current < (size_t) height) {
				current = 0;
			} else {
				current -= height;
				top_idx -= height;
			}
			break;
		case EKEY_NL:
			if (widgets.size() > 0)
				activate();
			return true;
		default:
			if (widgets.size() > 0)
				return widgets[current]->process_key(key);
			return false;
	}
	if (current != old_current) {
		widgets[old_current]->set_focus(false);
		widgets[current]->set_focus(has_focus);
		selection_changed();
	}
	ensure_cursor_on_screen();
	return true;
}

void list_pane_t::set_position(optint top, optint left) {
	if (!top.is_valid())
		top = t3_win_get_y(clip_window);
	if (!left.is_valid())
		left = t3_win_get_x(clip_window);

	t3_win_move(clip_window, top, left);
}

bool list_pane_t::set_size(optint _height, optint width) {
	int widget_width;
	bool result;

	if (_height.is_valid())
		height = _height;
	if (!width.is_valid())
		width = t3_win_get_width(clip_window);

	result = t3_win_resize(clip_window, height, width);
	result &= t3_win_resize(window, t3_win_get_height(window), width);

	widget_width = indicator ? (int) width - 2 : (int) width;

	for (widgets_t::iterator iter = widgets.begin();
			iter != widgets.end(); iter++)
		result &= (*iter)->set_size(None, widget_width);

	result &= scrollbar.set_size(height, None);

	ensure_cursor_on_screen();
	return result;
}


void list_pane_t::update_contents(void) {
	if (indicator)
		indicator_widget->set_position(current, 0);
	t3_win_move(window, -top_idx, 0);
	scrollbar.set_parameters(widgets.size(), top_idx, height);
	scrollbar.update_contents();
	for (widgets_t::iterator iter = widgets.begin(); iter != widgets.end(); iter++)
		(*iter)->update_contents();
}

void list_pane_t::set_focus(bool focus) {
	has_focus = focus;
	if (widgets.size() == 0)
		t3_term_hide_cursor();
	else if (current < widgets.size())
		widgets[current]->set_focus(has_focus);
}

bool list_pane_t::accepts_enter(void) { return true; }

void list_pane_t::reset(void) {
	top_idx = 0;
	current = 0;
}

void list_pane_t::update_positions(void) {
	widgets_t::iterator iter;
	size_t idx;
	int widget_width = t3_win_get_width(clip_window);
	if (indicator)
		widget_width -= 2;

	t3_win_resize(window, widgets.size(), t3_win_get_width(clip_window));
	for (iter = widgets.begin(), idx = 0;
			iter != widgets.end();
			iter++, idx++)
	{
		(*iter)->set_size(1, widget_width);
		(*iter)->set_position(idx, indicator ? 1 : 0);
	}
}

void list_pane_t::push_back(widget_t *widget) {
	widgets.push_back(widget);
	update_positions();
}

void list_pane_t::push_front(widget_t *widget) {
	widgets.push_front(widget);
	if (current + 1 < widgets.size())
		current++;
	update_positions();
}

void list_pane_t::pop_back(void) {
	if (current + 1 == widgets.size()) {
		widgets[current]->set_focus(false);
		if (current > 0) {
			current--;
			widgets[current]->set_focus(has_focus);
		}
	}
	widgets.pop_back();
}

void list_pane_t::pop_front(void) {
	if (current == 0) {
		widgets[0]->set_focus(false);
		if (widgets.size() > 1)
			widgets[1]->set_focus(has_focus);
	} else {
		current--;
	}
	widgets.pop_front();
}

widget_t *list_pane_t::back(void) {
	return (widget_t *) widgets.back();
}

widget_t *list_pane_t::operator[](int idx) {
	return (widget_t *) widgets[idx];
}

size_t list_pane_t::size(void) {
	return widgets.size();
}

bool list_pane_t::empty(void) {
	return widgets.empty();
}

list_pane_t::iterator list_pane_t::erase(list_pane_t::iterator position) {
	if (current == position && current + 1 == widgets.size()) {
		if (current != 0)
			current--;
	}
	widgets.erase(widgets.begin() + position);
	update_positions();
	return position;
}

list_pane_t::iterator list_pane_t::begin(void) {
	return 0;
}

list_pane_t::iterator list_pane_t::end(void) {
	return widgets.size();
}

int list_pane_t::get_current(void) {
	return current;
}

void list_pane_t::set_current(int idx) {
	if (idx < 0 || idx >= (int) widgets.size())
		return;

	current = idx;
	ensure_cursor_on_screen();
}

//=========== Indicator widget ================

list_pane_t::indicator_widget_t::indicator_widget_t(container_t *_parent) : widget_t(_parent, 1, 3), has_focus(false) {
	t3_win_set_depth(window, 1);
	set_size(None, 3);
}
bool list_pane_t::indicator_widget_t::process_key(key_t key) { (void) key; return false; }
void list_pane_t::indicator_widget_t::update_contents(void) {}

void list_pane_t::indicator_widget_t::set_focus(bool focus) {
	has_focus = focus;
}

bool list_pane_t::indicator_widget_t::set_size(optint _height, optint width) {
	bool result;

	(void) _height;

	if (!width.is_valid())
		return true;
	result = t3_win_resize(window, 1, width);
	t3_win_set_paint(window, 0, 0);
	t3_win_addch(window, T3_ACS_RARROW, T3_ATTR_ACS | (has_focus ? colors.dialog_selected_attrs : colors.dialog_attrs));
	t3_win_set_paint(window, 0, width - 1);
	t3_win_addch(window, T3_ACS_LARROW, T3_ATTR_ACS | (has_focus ? colors.dialog_selected_attrs : colors.dialog_attrs));
	return result;
}

t3_window_t *list_pane_t::indicator_widget_t::get_draw_window(void) {
	return window;
}

bool list_pane_t::indicator_widget_t::accepts_focus(void) { return false; }
