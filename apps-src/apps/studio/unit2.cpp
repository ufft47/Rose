/* $Id: mkwin_display.cpp 47082 2010-10-18 00:44:43Z shadowmaster $ */
/*
   Copyright (C) 2008 - 2010 by Tomasz Sniatowski <kailoran@gmail.com>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/
#define GETTEXT_DOMAIN "studio-lib"

#include "unit2.hpp"
#include "gettext.hpp"
#include "mkwin_display.hpp"
#include "mkwin_controller.hpp"
#include "gui/dialogs/mkwin_scene.hpp"
#include "gui/widgets/settings.hpp"
#include "gui/widgets/report.hpp"
#include "gui/auxiliary/window_builder/helper.hpp"
#include "game_config.hpp"

#include <boost/bind.hpp>

unit2::unit2(mkwin_controller& controller, mkwin_display& disp, unit_map& units, const std::pair<std::string, gui2::tcontrol_definition_ptr>& widget, unit* parent, const SDL_Rect& rect)
	: unit(controller, disp, units, widget, parent, -1)
{
	rect_ = rect;
}

unit2::unit2(mkwin_controller& controller, mkwin_display& disp, unit_map& units, int type, unit* parent, const SDL_Rect& rect)
	: unit(controller, disp, units, type, parent, -1)
{
	set_rect(rect);
}

unit2::unit2(const unit2& that)
	: unit(that)
{
}

void unit2::draw_unit()
{
	if (!loc_.valid()) {
		return;
	}

	if (refreshing_) {
		return;
	}
	trefreshing_lock lock(*this);

	const int xsrc = disp_.main_map_view().x + disp_.get_scroll_pixel_x(rect_.x);
	const int ysrc = disp_.main_map_view().y + disp_.get_scroll_pixel_y(rect_.y);

	const int zoom = disp_.hex_size();
	surface surf = create_neutral_surface(rect_.w, rect_.h);

	if (type_ == WIDGET) {
		redraw_widget(xsrc, ysrc, rect_.w, rect_.h);

	} else if (type_ == WINDOW) {
		disp_.drawing_buffer_add(display::LAYER_UNIT_DEFAULT, 
			loc_, image(), image::UNSCALED, xsrc, ysrc, zoom / 2, zoom / 2);

		if (!cell_.id.empty()) {
			surface text_surf = font::get_rendered_text2(cell_.id, -1, 10 * gui2::twidget::hdpi_scale, font::BLACK_COLOR);
			disp_.drawing_buffer_add(display::LAYER_UNIT_DEFAULT, loc_, text_surf, xsrc, ysrc + rect_.w / 2, 0, 0);
		}
		
	} else if (type_ == COLUMN) {
		int x = (loc_.x - 1) * image::tile_size;
		blit_integer_surface(x, surf, 0, 0);

	} else if (type_ == ROW) {
		int y = (loc_.y - 1) * image::tile_size;
		blit_integer_surface(y, surf, 0, 0);
	}

	blit_integer_surface(map_index_, surf, 0, 12);

	if (type_ == WINDOW || type_ == WIDGET) {
		//
		// draw rectangle
		//
		Uint32 border_color = 0x00ff00ff;
		if (loc_ == controller_.selected_hex()) {
			border_color = 0x0000ffff;
		} else if (this == controller_.last_unit()) {
			border_color = 0xff0000ff;
		}
		surface_lock locker(surf);

		// draw the border
		for (unsigned i = 0; i < 2; i ++) {
			const unsigned left = 0 + i;
			const unsigned right = left + rect_.w - (i * 2) - 1;
			const unsigned top = 0 + i;
			const unsigned bottom = top + rect_.h - (i * 2) - 1;

			// top horizontal (left -> right)
			draw_line(surf, border_color, left, top, right, top, true);

			// right vertical (top -> bottom)
			draw_line(surf, border_color, right, top, right, bottom, true);

			// bottom horizontal (left -> right)
			draw_line(surf, border_color, left, bottom, right, bottom, true);

			// left vertical (top -> bottom)
			draw_line(surf, border_color, left, top, left, bottom, true);
		}
	}

	disp_.drawing_buffer_add(display::LAYER_UNIT_DEFAULT, loc_, surf, xsrc, ysrc, 0, 0);
}

bool unit2::sort_compare(const base_unit& that_base) const
{
	const unit2* that = dynamic_cast<const unit2*>(&that_base);
	if (type_ != WIDGET) {
		if (that->type() == WIDGET) {
			return true;
		}
		return unit::sort_compare(that_base);
	}
	if (that->type() != WIDGET) {
		return false;
	}
	if (rect_.x != that->rect_.x || rect_.y != that->rect_.y) {
		return rect_.y < that->rect_.y || (rect_.y == that->rect_.y && rect_.x < that->rect_.x);
	}
	return unit::sort_compare(that_base);
}
