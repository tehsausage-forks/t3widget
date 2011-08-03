/* Copyright (C) 2011 G.P. Halkes
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
#ifndef T3_WIDGET_MENU_H
#define T3_WIDGET_MENU_H

#include <vector>
#include <t3widget/interfaces.h>
#include <t3widget/widgets/smartlabel.h>

namespace t3_widget {

class menu_panel_t;

/** Class implementing a menu bar. */
class T3_WIDGET_API menu_bar_t : public widget_t {
	friend class menu_panel_t;

	private:
		int current_menu, /**< Currently active window, when this menu_bar_t has the input focus. */
			old_menu; /**< Previously active window. */
		int start_col; /**< Column where the next menu will start. */
		bool hidden, /**< Boolean indicating whether this menu_bar_t has "hidden" display type. */
			/** Boolean indicating whether this menu_bar_t (or rather one of its menus) has the input focus.
			    See the comments at #set_focus for details.
			*/
			has_focus;

		std::vector<menu_panel_t *> menus; /**< Vector of menus used for this menu_bar_t. */

		/** Draw the name of a single menu in the menu bar. */
		void draw_menu_name(menu_panel_t *menu, bool selected);
		/** Draw all the names of the menus in the menu bar (unselected). */
		void draw(void);

		/** Close the currently open menu. */
		void close(void);
		/** Switch to the next menu. */
		void next_menu(void);
		/** Switch to the previous menu. */
		void previous_menu(void);
	public:
		/** Create a new menu_bar_t.
		    @param _hidden Boolean indicating whether this menu_bar_t has "hidden" display type.

		    A menu_bar_t can either be displayed continuously, or it can be hidden while none
		    of its menus are active. The latter option should only be used when the user
		    specifically asks for it, otherwise the user may not even be aware of the menu
		    bar's existance. */
		menu_bar_t(bool _hidden = false);
		/** Destroy the menu_bar_t.
		    Note that this does @b not destroy the menus contained by this menu_bar_t.
		*/
		~menu_bar_t(void);

		virtual bool process_key(key_t key);
		virtual bool set_size(optint height, optint width);
		virtual void update_contents(void);
		virtual void set_focus(bool focus);
		virtual void show(void);
		virtual bool is_hotkey(key_t key);
		virtual bool accepts_focus(void);
		/** Add a menu to the menu bar.
		    Note that this will be called automatically if the menu_bar_t is passed to the
		    menu_panel_t constructor. */
		void add_menu(menu_panel_t *menu);
		/** Remove a menu from the menu bar.
		    This does @b not destroy the menu.
		*/
		void remove_menu(menu_panel_t *menu);
		/** Set the "hidden" display property.
		    See #memu_bar_t for details.
		*/
		void set_hidden(bool _hidden);

	/** @fn sigc::connection connect_activate(const sigc::slot<void, int> &_slot)
	    Connect a callback to the #activate signal.
	*/
	/** Signal emitted when a menu item is selected.
	    The integer passed as the first argument is determined when creating the
	    menu item through menu_panel_t::add_item.
	*/
	T3_WIDGET_SIGNAL(activate, void, int);
};

}; // namespace
#endif
