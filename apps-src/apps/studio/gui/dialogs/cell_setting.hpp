/* $Id: campaign_difficulty.hpp 49603 2011-05-22 17:56:17Z mordante $ */
/*
   Copyright (C) 2010 - 2011 by Ignacio Riquelme Morelle <shadowm2006@gmail.com>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

#ifndef GUI_DIALOGS_CELL_SETTING_HPP_INCLUDED
#define GUI_DIALOGS_CELL_SETTING_HPP_INCLUDED

#include "gui/dialogs/dialog.hpp"
#include "gui/auxiliary/window_builder/helper.hpp"
#include "game_config.hpp"
#include <vector>

namespace gui2 {

extern const std::string untitled;
void show_id_error(display& disp, const std::string& id, const std::string& errstr);
bool verify_formula(display& disp, const std::string& id, const std::string& expression2);

struct tlayout
{
	tlayout(int val, const std::string& description)
		: val(val)
		, description(description)
	{
		std::string prefix = "layout/";
		if (val == tgrid::HORIZONTAL_GROW_SEND_TO_CLIENT) {
			icon = prefix + "align-left-grow.png";
			id = null_str; // set horizontal_grow.
		} else if (val == tgrid::HORIZONTAL_ALIGN_LEFT) {
			icon = prefix + "align-left.png";
			id = "left";
		} else if (val == tgrid::HORIZONTAL_ALIGN_CENTER) {
			icon = prefix + "align-h-center.png";
			id = "center";
		} else if (val == tgrid::HORIZONTAL_ALIGN_RIGHT) {
			icon = prefix + "align-right.png";
			id = "right";
		} else if (val == tgrid::VERTICAL_GROW_SEND_TO_CLIENT) {
			icon = prefix + "align-top-grow.png";
			id = null_str; // set vertical_grow.
		} else if (val == tgrid::VERTICAL_ALIGN_TOP) {
			icon = prefix + "align-top.png";
			id = "top";
		} else if (val == tgrid::VERTICAL_ALIGN_CENTER) {
			icon = prefix + "align-v-center.png";
			id = "center";
		} else if (val == tgrid::VERTICAL_ALIGN_BOTTOM) {
			icon = prefix + "align-bottom.png";
			id = "bottom";
		}
	}

	int val;
	std::string id;
	std::string icon;
	std::string description;
};

extern std::map<int, tlayout> horizontal_layout;
extern std::map<int, tlayout> vertical_layout;

struct tscroll_mode
{
	tscroll_mode(int val, const std::string& description)
		: val(val)
		, id()
		, description(description)
	{
		if (val == tscroll_container::always_invisible) {
			id = "never";
		} else if (val == tscroll_container::auto_visible) {
			id = "auto";
		}
	}

	int val;
	std::string id;
	std::string description;
};
extern std::map<int, tscroll_mode> horizontal_mode;
extern std::map<int, tscroll_mode> vertical_mode;

struct tparam3
{
	tparam3(int val, const std::string& id, const std::string& description)
		: val(val)
		, id(id)
		, description(description)
	{}

	int val;
	std::string id;
	std::string description;
};
extern std::map<int, tparam3> orientations;

void init_layout_mode();

struct tcell_setting {
	tcell_setting()
	{
		widget.cell.border_size_ = 0;
		widget.cell.flags_ = gui2::tgrid::HORIZONTAL_ALIGN_CENTER | gui2::tgrid::VERTICAL_ALIGN_CENTER;
		widget.horizontal_mode = tscroll_container::always_invisible;
		widget.vertical_mode = tscroll_container::auto_visible;
		widget.restrict_width = false;
		widget.size_is_max = false;
		widget.text_font_size = 0;
		widget.text_color_tpl = 0;
		widget.gc = false;
		widget.drag = twidget::drag_none;

		widget.tree_view.indention_step_size = 12;
		widget.tree_view.node_id = "default";
		widget.tree_view.node_definition = "default";

		widget.report.multi_line = false;
		widget.report.toggle = false;
		widget.report.definition = "default";
		std::string unit_width;
		std::string unit_height;
		widget.report.gap = 0;
		widget.report.segment_switch = false;
		widget.report.fixed_cols = 0;

		widget.text_box.password = false;

		widget.stack.mode = tstack::pip;

		window.app = "rose";
		window.click_dismiss = false;
		window.definition = "default";
		window.orientation = twidget::auto_orientation;
		window.scene = false;
		window.tile_shape = game_config::tile_square;
		window.automatic_placement = true;
		window.horizontal_placement = gui2::tgrid::HORIZONTAL_ALIGN_CENTER;
		window.vertical_placement = gui2::tgrid::VERTICAL_ALIGN_CENTER;
		window.x = "(screen_width - width) / 2";
		window.y = "(screen_height - height) / 2";
		window.width = "if(screen_width < 800, screen_width, 800)";
		window.height = "if(screen_height < 600, screen_height, 600)";
		window.cover_width = 0;
		window.cover_height = 0;
		window.color = 0xffffffff;

		row.grow_factor = 0;
		column.grow_factor = 0;
		grid.grow_factor = 0;
	}

	std::string id;
	struct {
		gui2::tgrid::tchild cell;
		std::string linked_group;
		std::string width;
		std::string height;
		std::string label;
		std::string label_textdomain;
		std::string tooltip;
		std::string tooltip_textdomain;
		bool restrict_width;
		bool size_is_max;
		int text_font_size;
		int text_color_tpl;
		bool gc;
		unsigned drag;

		tscroll_container::tscrollbar_mode horizontal_mode;
		tscroll_container::tscrollbar_mode vertical_mode;

		struct {
			std::string x;
			std::string y;
			std::string w;
			std::string h;
			std::string name;
		} draw;

		struct {
			int indention_step_size;
			std::string node_id;
			std::string node_definition;
		} tree_view;

		struct {
			std::vector<tlinked_group> linked_groups;
		} listbox;

		struct {
			bool multi_line;
			bool toggle;
			std::string definition;
			std::string unit_width;
			std::string unit_height;
			int gap;
			bool segment_switch;
			int fixed_cols;
		} report;

		struct {
			bool password;
		} text_box;

		struct {
			std::string text_box;
		} text_box2;

		struct {
			int minimum_value;
			int maximum_value;
			int step_size;
		} slider;

		struct {
			tstack::tmode mode;
		} stack;
	} widget;

	struct {
		std::string app;
		std::string definition;
		twidget::torientation orientation;
		bool scene;
		bool click_dismiss;
		bool automatic_placement;
		std::string x;
		std::string y;
		std::string width;
		std::string height;
		std::string tile_shape;
		int horizontal_placement;
		int vertical_placement;
		int cover_width;
		int cover_height;
		Uint32 color;
	} window;

	struct {
		int grow_factor;
	} row;

	struct {
		int grow_factor;
	} column;

	struct {
		int grow_factor;
	} grid;
};

class tsetting_dialog: public tdialog
{
public:
	static const int min_id_len = 1;
	static const int max_id_len = 32;

	tsetting_dialog(const tcell_setting& cell)
		: cell_(cell)
	{}

	const tcell_setting& cell() const { return cell_; }

protected:
	tcell_setting cell_;
};

}


#endif /* ! GUI_DIALOGS_CAMPAIGN_DIFFICULTY_HPP_INCLUDED */
