#ifndef UNIT_HPP_INCLUDED
#define UNIT_HPP_INCLUDED

#include "base_unit.hpp"

class simple_controller;
class simple_display;
class unit_map;

class unit: public base_unit
{
public:
	unit(simple_controller& controller, simple_display& disp, unit_map& units);

	void draw_unit() override;

protected:
	simple_controller& controller_;
	simple_display& disp_;
	unit_map& units_;
};

#endif
