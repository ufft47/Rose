/* $Id: title_screen.cpp 48740 2011-03-05 10:01:34Z mordante $ */
/*
   Copyright (C) 2008 - 2011 by Mark de Wever <koraq@xs4all.nl>
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

#include "gui/dialogs/rose.hpp"

#include "display.hpp"
#include "game_config.hpp"
#include "preferences.hpp"
#include "gettext.hpp"
#include "formula_string_utils.hpp"
#include "gui/dialogs/message.hpp"
#include "gui/dialogs/combo_box.hpp"
#include "gui/dialogs/browse.hpp"
#include "gui/dialogs/capabilities.hpp"
#include "gui/dialogs/menu.hpp"
#include "gui/dialogs/new_window.hpp"
#include "gui/dialogs/new_theme.hpp"
#include "gui/widgets/button.hpp"
#include "gui/widgets/label.hpp"
#include "gui/widgets/settings.hpp"
#include "gui/widgets/text_box.hpp"
#include "gui/widgets/tree.hpp"
#include "gui/widgets/toggle_button.hpp"
#include "gui/widgets/toggle_panel.hpp"
#include "gui/widgets/listbox.hpp"
#include "gui/widgets/track.hpp"
#include "gui/widgets/stack.hpp"
#include "gui/widgets/window.hpp"
#include "preferences_display.hpp"
#include "help.hpp"
#include "version.hpp"
#include "filesystem.hpp"
#include "loadscreen.hpp"
#include <hero.hpp>
#include <time.h>
#include "sound.hpp"
#include "base_instance.hpp"
#include "formula_debugger.hpp"

#include "gui/dialogs/simple_item_selector.hpp"

#include <boost/bind.hpp>

#include <algorithm>
#include <iomanip>

#include "sdl_image.h"


#ifndef _WIN32
typedef struct {
    unsigned long  Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char  Data4[ 8 ];
} GUID;

void CoCreateGuid(GUID* pguid)
{
	memset(pguid, 0, sizeof(GUID));
}
#else
#include <combaseapi.h>
#endif

std::string guid_2_str(const GUID& guid)
{
	std::stringstream ss;

	ss << std::setw(8) << std::setfill('0') << std::setbase(16) << guid.Data1 << "-";
	ss << std::setw(4) << std::setfill('0') << std::setbase(16) << guid.Data2 << "-";
	ss << std::setw(4) << std::setfill('0') << std::setbase(16) << guid.Data3 << "-";
	ss << std::setw(2) << std::setfill('0') << std::setbase(16) << (int)guid.Data4[0];
	ss << std::setw(2) << std::setfill('0') << std::setbase(16) << (int)guid.Data4[1] << "-";
	for (int at = 2; at < 8; at ++) {
		ss << std::setw(2) << std::setfill('0') << std::setbase(16) << (int)guid.Data4[at];
	}

	return utils::uppercase(ss.str());
}

extern terrain_builder::building_rule* wml_building_rules_from_file(const std::string& fname, uint32_t* rules_size_ptr);

namespace gui2 {

REGISTER_DIALOG(studio, rose)

trose::trose(display& disp, hero& player_hero)
	: disp_(disp)
	, player_hero_(player_hero)
	, window_(NULL)
	, build_msg_data_(build_msg_data(build_normal, null_str))
	, current_copier_(NULL)
	, br_rules_(nullptr)
	, br_rules_size_(0)
	, gap_argb_(0xffd4d4d4) // 0xff344768, 0xffe8e8ec
{
}

trose::~trose()
{
	br_release_heap();
}

static const char* menu_items[] = {
	"window",
	"animation",
	"language",
	"chat",
	"preferences",
	"design"
};
static int nb_items = sizeof(menu_items) / sizeof(menu_items[0]);

static std::vector<int> ids;
void trose::pre_show(CVideo& video, twindow& window)
{
	ids.clear();
	window_ = &window;

	set_restore(false);
	window.set_escape_disabled(true);

	// Set the version number
	tcontrol* control = find_widget<tcontrol>(&window, "revision_number", false, false);
	if (control) {
		control->set_label(_("V") + game_config::version);
	}
	window.canvas()[0].set_variable("revision_number", variant(_("Version") + std::string(" ") + game_config::version));
	window.set_label("misc/white-background.png");

	/***** Set the logo *****/
	tcontrol* logo = find_widget<tcontrol>(&window, "logo", false, false);
	if (logo) {
		logo->set_label(game_config::logo_png);
	}

	tbutton* b;
	for (int item = 0; item < nb_items; item ++) {
		b = find_widget<tbutton>(&window, menu_items[item], false, false);
		if (!b) {
			continue;
		}
		std::string str = std::string("icons/") + menu_items[item] + ".png";

		b->set_canvas_variable("icon", variant(str));
	}

	for (int item = 0; item < nb_items; item ++) {
		std::string id = menu_items[item];
		int retval = twindow::NONE;
		if (id == "window") {
			retval = WINDOW;
		} else if (id == "animation") {
			retval = ANIMATION;
		} else if (id == "language") {
			retval = CHANGE_LANGUAGE;
		} else if (id == "chat") {
			retval = MESSAGE;
		} else if (id == "preferences") {
			retval = EDIT_PREFERENCES;
		} else if (id == "design") {
			retval = DESIGN;
		}

		connect_signal_mouse_left_click(
			find_widget<tbutton>(&window, id, false)
			, boost::bind(
				&trose::set_retval
				, this
				, boost::ref(window)
				, retval));
	}

	tlobby::thandler::join();

	refresh_explorer_tree(window);
	pre_base(window);
}

void trose::refresh_explorer_tree(twindow& window)
{
	cookies_.clear();

	ttree* explorer = find_widget<ttree>(&window, "explorer", false, true);
	ttree_node& root = explorer->get_root_node();
	explorer->clear();
	VALIDATE(explorer->empty(), null_str);

	ttree_node& htvi = add_explorer_node(game_config::path, root, file_name(game_config::path), true);
	::walk_dir(editor_.working_dir(), false, boost::bind(
				&trose::walk_dir2
				, this
				, _1, _2, &htvi));
	htvi.sort_children(boost::bind(&trose::compare_sort, this, _1, _2));
	htvi.unfold();
}

void trose::pre_base(twindow& window)
{
/*
	game_logic::map_formula_callable variables;
	variables.add("text_width", variant(120));
	variables.add("width", variant(160));
	game_logic::formula_debugger debugger;
	game_logic::formula f("(if(text_width > width, 0, (width - text_width) / 2))", nullptr);
	int v = f.evaluate(variables, &debugger).as_int();
*/
	tbuild::pre_show(find_widget<ttrack>(&window, "task_status", false));

	ttrack* splitter = find_widget<ttrack>(&window, "splitter", false, true);
	splitter->set_did_draw(boost::bind(&trose::did_draw_splitter, this, _1, _2, _3));

	tcontrol* img = find_widget<tcontrol>(&window, "top_gap", false, true);
	// img->set_blits(image::tblit(gap_argb_, twidget::npos, twidget::npos));
	img->set_blits(tformula_blit(image::BLITM_RECT, null_str, null_str, "(width)", "(height)", gap_argb_));

/*
	img = find_widget<tcontrol>(&window, "bottom_gap", false, true);
	img->set_blits(image::tblit(gap_argb_, twidget::npos, twidget::npos));
*/
	connect_signal_mouse_left_click(
			find_widget<tbutton>(&window, "browse", false)
			, boost::bind(
				&trose::set_working_dir
				, this
				, boost::ref(window)));
	find_widget<tbutton>(&window, "browse", false).set_active(false);

	connect_signal_mouse_left_click(
			find_widget<tbutton>(&window, "refresh", false)
			, boost::bind(
				&trose::do_refresh
				, this
				, boost::ref(window)));

	connect_signal_mouse_left_click(
			find_widget<tbutton>(&window, "build", false)
			, boost::bind(
				&trose::do_normal_build
				, this
				, boost::ref(window)
				, false));

	ttree* explorer = find_widget<ttree>(&window, "explorer", false, true);
	explorer->set_did_fold_changed(boost::bind(&trose::did_explorer_icon_folded, this, _2, _3));
	explorer->set_did_double_click(boost::bind(&trose::did_left_double_click_explorer, this, _2));
	explorer->set_did_right_button_up(boost::bind(&trose::right_click_explorer, this, _2, _3));

	ttree* wml = find_widget<ttree>(&window, "wml", false, true);
	wml->set_did_fold_changed(boost::bind(&trose::did_wml_folded, this, _2, _3));

	tstack* stack = find_widget<tstack>(&window, "stack", false, true);
	stack->set_radio_layer(0);

	reload_mod_configs(disp_);
	fill_items(window);
}

ttree_node& trose::add_explorer_node(const std::string& dir, ttree_node& parent, const std::string& name, bool isdir)
{
	std::map<std::string, std::string> tree_group_item;

	tree_group_item["text"] = name;
	ttree_node& htvi = parent.insert_node("default", tree_group_item, twidget::npos, isdir);
	htvi.set_child_icon("text", isdir? "misc/folder.png": "misc/file.png");

	cookies_.insert(std::make_pair(&htvi, tcookie(dir, name, isdir)));
	return htvi;
}

bool trose::compare_sort(const ttree_node& a, const ttree_node& b)
{
	const tcookie& a2 = cookies_.find(&a)->second;
	const tcookie& b2 = cookies_.find(&b)->second;

	if (a2.isdir && !b2.isdir) {
		// tvi1 is directory, tvi2 is file
		return true;
	} else if (!a2.isdir && b2.isdir) {
		// tvi1 is file, tvi2 if directory
		return false;
	} else {
		// both lvi1 and lvi2 are directory or file, use compare string.
		return SDL_strcasecmp(a2.name.c_str(), b2.name.c_str()) <= 0? true: false;
	}
}

bool trose::walk_dir2(const std::string& dir, const SDL_dirent2* dirent, ttree_node* root)
{
	bool isdir = SDL_DIRENT_DIR(dirent->mode);
	add_explorer_node(dir, *root, dirent->name, isdir);

	return true;
}

void trose::did_explorer_icon_folded(ttree_node& node, const bool folded)
{
	if (!folded && node.empty()) {
		const tcookie& cookie = cookies_.find(&node)->second;
		::walk_dir(cookie.dir + '/' + cookie.name, false, boost::bind(
				&trose::walk_dir2
				, this
				, _1, _2, &node));
		node.sort_children(boost::bind(&trose::compare_sort, this, _1, _2));
	}
}

void trose::did_left_double_click_explorer(ttree_node& node)
{
	const tcookie& cookie = cookies_.find(&node)->second;
	const std::string prefix = game_config::path + "/xwml";
	if (cookie.dir.find(prefix) != 0) {
		return;
	}
	if (cookie.name.find(".bin") != std::string::npos) {
		wml_config_from_file(cookie.dir + '/' + cookie.name, current_wml_.second, nullptr, nullptr, nullptr);
		VALIDATE(!current_wml_.second.empty(), null_str);
		wml_2_tv(cookie.name, current_wml_.second, -1);

	} else if (cookie.name.find("tb-") == 0 && cookie.name.find(".dat") != std::string::npos) {
		current_wml_.second.clear();
		// use the swap trick to clear the rules cache and get a fresh one.
		// because simple clear() seems to cause some progressive memory degradation.
		// terrain_builder::building_ruleset empty;
		// std::swap(vtb.rules_, empty);
		br_release_heap();
		br_rules_ = wml_building_rules_from_file(cookie.dir + '/' + cookie.name, &br_rules_size_);
		br_2_tv(cookie.name, br_rules_, br_rules_size_);
	}
}

bool trose::handle(int tag, tsock::ttype type, const config& data)
{
	if (tag != tlobby::tag_chat) {
		return false;
	}

	if (type != tsock::t_data) {
		return false;
	}
	if (const config& c = data.child("whisper")) {
		tbutton* b = find_widget<tbutton>(window_, "message", false, false);
		if (b->label().empty()) {
			b->set_label("misc/red-dot12.png");
		}
		sound::play_UI_sound(game_config::sounds::receive_message);
	}
	return false;
}

void trose::set_retval(twindow& window, int retval)
{
	if (is_building()) {
		return;
	}
	window.set_retval(retval);
}

void trose::fill_items(twindow& window)
{
	if (!file_exists(editor_.working_dir() + "/data/core/_main.cfg") || !is_directory(editor_.working_dir() + "/xwml")) {
		return;
	}

	editor_.reload_extendable_cfg();

	std::vector<teditor_::BIN_TYPE> system_bins;
	for (teditor_::BIN_TYPE type = teditor_::BIN_MIN; type <= teditor_::BIN_SYSTEM_MAX; type = (teditor_::BIN_TYPE)(type + 1)) {
		system_bins.push_back(type);
	}
	editor_.get_wml2bin_desc_from_wml(system_bins);
	const std::vector<std::pair<teditor_::BIN_TYPE, teditor_::wml2bin_desc> >& descs = editor_.wml2bin_descs();

	bool enable_build = false, require_build = false;
	std::stringstream ss;
	tlistbox* list = find_widget<tlistbox>(&window, "items", false, true);
	list->clear();
	for (std::vector<std::pair<teditor_::BIN_TYPE, teditor_::wml2bin_desc> >::const_iterator it = descs.begin(); it != descs.end(); ++ it) {
		bool enable_build2 = false;
		const teditor_::wml2bin_desc& desc = it->second;

		std::map<std::string, std::string> list_item_item;

		list_item_item.insert(std::make_pair("filename", desc.bin_name));
		list_item_item.insert(std::make_pair("app", desc.app));

		ss.str("");
		ss << "(" << desc.wml_nfiles << ", " << desc.wml_sum_size << ", " << desc.wml_modified << ")";
		list_item_item.insert(std::make_pair("wml_checksum", ss.str()));

		ss.str("");
		ss << "(" << desc.bin_nfiles << ", " << desc.bin_sum_size << ", " << desc.bin_modified << ")";
		list_item_item.insert(std::make_pair("bin_checksum", ss.str()));

		ttoggle_panel& panel = list->insert_row(list_item_item);
		ss.str("");
		if (desc.wml_nfiles == desc.bin_nfiles && desc.wml_sum_size == desc.bin_sum_size && desc.wml_modified == desc.bin_modified) {
			ss << "misc/ok.png";
		} else {
			ss << "misc/alert.png";
		}
		panel.set_child_icon("filename", ss.str());

		ttoggle_button* prefix = find_widget<ttoggle_button>(&panel, "prefix", false, true);
		if (enable_build) {
			enable_build2 = true;

		} else if (build_msg_data_.type == build_export || build_msg_data_.type == build_ios_kit) {
			enable_build2 = true;

		} else if (desc.wml_nfiles != desc.bin_nfiles || desc.wml_sum_size != desc.bin_sum_size || desc.wml_modified != desc.bin_modified) {	
			enable_build2 = true;
			if (it->first == teditor_::MAIN_DATA) {
				enable_build = true;
			}

		} else if (it->first == teditor_::SCENARIO_DATA) {
			if (desc.wml_nfiles != desc.bin_nfiles || desc.wml_sum_size != desc.bin_sum_size || desc.wml_modified != desc.bin_modified) {
				const std::string id = file_main_name(desc.bin_name);
				if (app_bin_id_.empty() || id == app_bin_id_) {
					enable_build2 = true;
				}
			}
		}
		if (!desc.wml_nfiles) {
			prefix->set_active(false);

		} else if (enable_build2) {
			prefix->set_value(true);
			require_build = true;
		}

		prefix->set_did_state_changed(boost::bind(&trose::check_build_toggled, this, _1));
	}

	find_widget<tbutton>(&window, "build", false).set_active(enable_build);
}

void trose::set_build_active(twindow& window)
{
	tlistbox* list = find_widget<tlistbox>(&window, "items", false, true);
	int count = list->rows();
	for (int at = 0; at < count; at ++) {
		twidget& panel = list->row_panel(at);
		ttoggle_button* prefix = find_widget<ttoggle_button>(&panel, "prefix", false, true);
		if (prefix->get_value()) {
			find_widget<tbutton>(&window, "build", false).set_active(true);
			return;
		}
	}
	find_widget<tbutton>(&window, "build", false).set_active(false);
}

void trose::check_build_toggled(twidget& widget)
{
	set_build_active(*window_);
}

void trose::new_app(const tapp_capabilities& capabilities)
{
	std::stringstream err;
	utils::string_map symbols;

	const std::string& app = capabilities.app;

	symbols["app"] = app;
	symbols["result"] = _("Fail");
	err << vgettext2("New $app, $result.", symbols) << "\n\n";

	GUID guid;
	CoCreateGuid(&guid);
	std::string guid_str = guid_2_str(guid);

	std::string src_path = directory_name2(game_config::path) + "/apps-src";
	std::string projectfiles = src_path + "/apps/projectfiles";
	if (!SDL_IsDirectory(projectfiles.c_str())) {
		err << "Current not in Work Kit!";
		gui2::show_message(disp_.video(), null_str, err.str());
		return;
	}

	newer->set_app(app);
	if (!newer->handle(disp_)) {
		err << "Handle task fail!";
		gui2::show_message(disp_.video(), null_str, err.str());
		return;
	}

	std::vector<std::pair<std::string, std::string> > replaces;
	//
	// prepare project files
	//
	const std::string app_in_prj = "studio";

	std::string src = projectfiles + "/vc/" + app + ".vcxproj";
	replaces.push_back(std::make_pair(studio_guid, guid_str));
	file_replace_string(src, replaces);

	if (!apps_sln::add_project(app, guid_str)) {
		err << "Modify apps.sln fail!";
		gui2::show_message(disp_.video(), null_str, err.str());
		return;
	}

	generate_capabilities_cfg2(game_config::path, capabilities);
	build_on_app_changed(capabilities.app, *window_, false);
}

void trose::do_new_window(tapp_copier& copier, const std::string& id, bool scene, bool unit_files)
{
	VALIDATE(!id.empty(), null_str);

	::tnew_window* new_window = nullptr;
	if (scene) {
		new_window = dynamic_cast<::tnew_window*>(new_scene.get());
	} else {
		new_window = dynamic_cast<::tnew_window*>(new_dialog.get());
	}
	std::stringstream err;
	utils::string_map symbols;
	
	symbols["app"] = copier.app;
	symbols["id"] = id;
	err << vgettext2("New dialog(id = $id) to $app fail!", symbols);

	new_window->set_app(copier.app, id);
	if (scene) {
		new_scene->set_unit_files(unit_files);
	}
	if (!new_window->handle(disp_)) {
		err << "\n\n";
		err << "task fail!";
		gui2::show_message(disp_.video(), null_str, err.str());
		return;
	}
	if (!apps_sln::new_dialog(copier.app, new_window->sln_files())) {
		new_window->fail_rollback(disp_);
		err << "\n\n";
		err << "modify vcxproj fail!";
		gui2::show_message(disp_.video(), null_str, err.str());
		return;
	}

	build_msg_data_.set(build_new_dialog, copier.app, id);
	fill_items(*window_);
	do_build(*window_);
}

void trose::do_remove_dialog(tapp_copier& copier, const std::string& id, bool scene)
{
	std::vector<std::string> files;
	files.push_back(game_config::apps_src_path + "/apps/" + copier.app + "/gui/dialogs/" + id + ".cpp");
	files.push_back(game_config::apps_src_path + "/apps/" + copier.app + "/gui/dialogs/" + id + ".hpp");

	std::string base_path = game_config::path + "/data/gui/" + game_config::generate_app_dir(copier.app) + "/" + (scene? "scene": "window");
	files.push_back(base_path + "/" + id + ".cfg");

	for (std::vector<std::string>::const_iterator it = files.begin(); it != files.end(); ++ it) {
		const std::string& file = *it;
		SDL_DeleteFiles(file.c_str());
	}
}

void trose::do_new_theme(tapp_copier& copier, const std::string& id, const theme::tfields& result)
{
}

void trose::export_app(tapp_copier& copier)
{
	std::stringstream ss;
	utils::string_map symbols;

	symbols["app"] = copier.app;
	symbols["dst"] = copier.exporter->alias_2_path(ttask::app_src2_alias);
	ss.str("");
	ss << vgettext2("Do you want to export $app package to $dst?", symbols); 

	int res = gui2::show_message(disp_.video(), "", ss.str(), gui2::tmessage::yes_no_buttons);
	if (res != gui2::twindow::OK) {
		return;
	}

	absolute_draw();
	bool fok = copier.exporter->handle(disp_);
	if (fok && is_studio_app(copier.app)) {
		fok = copier.studio_extra_exporter->handle(disp_);
	}
	if (!fok) {
		symbols["src"] = copier.exporter->alias_2_path(ttask::src2_alias);
		symbols["result"] = fok? _("Success"): _("Fail");
		ss.str("");
		ss << vgettext2("Export $app package from \"$src\" to \"$dst\", $result!", symbols);
		gui2::show_message(disp_.video(), null_str, ss.str());
		return;
	}

	generate_app_cfg(copier.exporter->alias_2_path(ttask::app_res_alias), copier.app);

	current_copier_ = &copier;
	{
		// build
		build_msg_data_.set(build_export, copier.app);
		on_change_working_dir(*window_, copier.exporter->alias_2_path(ttask::app_res_alias));
		do_build(*window_);
	}
}

bool trose::copy_android_res(const tapp_copier& copier, bool silent)
{
	std::stringstream ss;
	utils::string_map symbols;

	bool fok = copier.android_res_copier->handle(disp_);
	if (!silent) {
		symbols["app"] = copier.app;
		symbols["src"] = copier.android_res_copier->alias_2_path(ttask::app_res_alias);
		symbols["dst"] = copier.android_res_copier->alias_2_path(tapp_copier::app_android_prj_alias);
		symbols["result"] = fok? _("Success"): _("Fail");
		ss.str("");
		ss << vgettext2("Copy $app|'s resource from \"$src\" to \"$dst\", $result!", symbols);
		gui2::show_message(disp_.video(), null_str, ss.str());
	}
	return fok;
}

bool trose::export_ios_kit(const tapp_copier& copier)
{
	std::stringstream ss;
	utils::string_map symbols;

	bool fok = copier.ios_kiter->handle(disp_);
	if (!fok) {
		symbols["dst"] = copier.ios_kiter->alias_2_path(tios_kit::kit_alias);
		symbols["result"] = fok? _("Success"): _("Fail");
		ss.str("");
		ss << vgettext2("Create iOS kit on \"$dst\", $result!", symbols);
		gui2::show_message(disp_.video(), null_str, ss.str());
	}
	generate_app_cfg(copier.ios_kiter->alias_2_path(tios_kit::studio_alias), copier.app);

	current_copier_ = &copier;
	{
		// build
		build_msg_data_.set(build_ios_kit, copier.app);
		on_change_working_dir(*window_, copier.ios_kiter->alias_2_path(tios_kit::studio_alias));
		do_build(*window_);
	}

	return fok;
}

void trose::import_apps(const std::string& import_res)
{
	std::stringstream err;
	utils::string_map symbols;
	symbols["res"] = import_res;

	// check rose's version. this version must >= import version.
	{
		std::string import_version;
		game_config::config_cache& cache = game_config::config_cache::instance();
		config cfg;
		cache.get_config(import_res + "/data/rose_config.cfg", cfg);
		const config& rose_cfg = cfg.child("rose_config");
		if (rose_cfg) {
			import_version = rose_cfg["version"].str();
		}
		if (import_version.empty()) {
			err << vgettext2("Unknown Rose version in $res. Can not import.", symbols);

		} else if (!do_version_check(version_info(game_config::version), OP_GREATER_OR_EQUAL, version_info(import_version))) {
			err << vgettext2("Work Kit's Rose version is less than in $res. Can not import.", symbols);
		}
		if (!err.str().empty()) {
			gui2::show_message(disp_.video(), "", err.str());
			return;
		}
	}

	std::set<std::string> apps_in_res;
	::walk_dir(import_res, false, boost::bind(
				&trose::collect_app
				, this
				, _1, _2, boost::ref(apps_in_res)));
	VALIDATE(apps_in_res.size() > 0, null_str);

	std::set<std::string> this_apps;
	for (std::vector<std::unique_ptr<tapp_copier> >::const_iterator it = app_copiers.begin(); it != app_copiers.end(); ++ it) {
		const tapp_copier& copier = **it;
		this_apps.insert(copier.app);
	}

	std::string dir = directory_name2(import_res);
	std::string app_res = file_name(import_res);
	std::string src2_path;
	if (app_res == "apps-res") {
		src2_path = dir + "/apps-src/apps";
	} else {
		// git ride of "-res"
		const std::string app = app_res.substr(0, app_res.size() - 4);
		src2_path = dir + "/" + app + "-src/" + app;
	}

	GUID guid;
	std::string proj_guid_str;
	const std::string app_in_prj = "studio";
	const std::string projectfiles = game_config::apps_src_path + "/apps/projectfiles";
	std::vector<std::pair<std::string, std::string> > replaces;

	std::map<std::string, std::string> apps_in_sln = apps_sln::apps_in(src2_path);
	std::string err_app;
	std::set<std::string> imported_apps;
	for (std::set<std::string>::const_iterator it = apps_in_res.begin(); it != apps_in_res.end(); ++ it) {
		const std::string& app = *it;
		std::map<std::string, std::string>::const_iterator it2 = apps_in_sln.find(app);
		if (it2 == apps_in_sln.end()) {
			continue;
		}
		if (this_apps.find(app) != this_apps.end()) {
			continue;
		}
		importer->set_app(app, import_res, src2_path);
		if (!importer->handle(disp_)) {
			err_app = app;
			break;
		}
		
		// create new project's guid and use it.
		CoCreateGuid(&guid);
		proj_guid_str = guid_2_str(guid);

		replaces.clear();
		replaces.push_back(std::make_pair(it2->second, proj_guid_str));
		replaces.push_back(std::make_pair(utils::lowercase(it2->second), proj_guid_str));
		file_replace_string(projectfiles + "/vc/" + app + ".vcxproj", replaces);

		// insert this project to apps.sln
		if (!apps_sln::add_project(app, proj_guid_str)) {
			importer->fail_rollback(disp_);
			err_app = app;
			break;
		}

		imported_apps.insert(app);
	}

	if (!err_app.empty()) {
		symbols["app"] = err_app;
		err << vgettext2("Import $app from $res fail!", symbols); 

	} else if (imported_apps.empty()) {
		err << vgettext2("There is no importable app in $res!", symbols);

	} else {
		for (std::set<std::string>::const_iterator it = imported_apps.begin(); it != imported_apps.end(); ++ it) {
			const std::string app = *it;
			this_apps.insert(app);
		}
		validater_res(this_apps);

		build_msg_data_.set(build_import, imported_apps);
		fill_items(*window_);
		do_build(*window_);
		return;
	}
	gui2::show_message(disp_.video(), "", err.str());
}

void trose::on_change_working_dir(twindow& window, const std::string& dir)
{
	editor_.set_working_dir(dir);
	fill_items(window);

	task_status_->set_dirty();
	require_set_task_bar_ = true;
}

void trose::build_on_app_changed(const std::string& app, twindow& window, bool remove)
{
	// update three-app.cfg
	std::set<std::string> apps;
	for (std::vector<std::unique_ptr<tapp_copier> >::const_iterator it = app_copiers.begin(); it != app_copiers.end(); ++ it) {
		const tapp_copier& copier = **it;
		if (!remove || copier.app != app) {
			apps.insert(copier.app);
		}
	}
	if (!remove) {
		apps.insert(app);
	}
	validater_res(apps);

	build_msg_data_.set(remove? build_remove: build_new, app);

	fill_items(window);
	do_build(window);
}

bool trose::browse_import_res_changed(const std::string& path, const std::string& terminate)
{
	const std::string path2 = utils::normalize_path(path);
	if (path2 == game_config::path) {
		return false;
	}

	std::set<std::string> apps_in_res;
	::walk_dir(path2, false, boost::bind(
				&trose::collect_app
				, this
				, _1, _2, boost::ref(apps_in_res)));

	return apps_in_res.size() >= 1;
}

void walk_cfg_recursion(const config& cfg, ttree_node& htvroot, int deep, const int maxdeep)
{
	// size_t attribute_index, cfg_index = 0;
	
	std::map<std::string, std::string> data;
	std::stringstream ss;
	// attribute
	// attribute_index = 0;
	BOOST_FOREACH (const config::attribute &istrmap, cfg.attribute_range()) {
		data.clear();
		ss.str("");
		ss << istrmap.first << "=" << istrmap.second.str();
		data["label"] = ss.str();
		ttree_node& node = htvroot.insert_node("default", data);
		node.set_child_icon("label", "misc/file.png");
	}
	// recursively resolve children
	int cfg_index = 0;
	BOOST_FOREACH (const config::any_child& value, cfg.all_children_range()) {
		// key
		ss.str("");
		ss << "[" << cfg_index << "]" << value.key;
		data["label"] = ss.str();
		ttree_node& htvi = htvroot.insert_node("default", data, twidget::npos, true);
		htvi.set_child_icon("label", "misc/folder.png");
		htvi.set_cookie(reinterpret_cast<size_t>(&value.cfg));

		cfg_index ++;
		if (maxdeep >= deep) {
			walk_cfg_recursion(value.cfg, htvi, deep + 1, maxdeep);
		}
	}
}

void trose::wml_2_tv(const std::string& name, const config& cfg, const uint32_t maxdeep)
{
	tstack* stack = find_widget<tstack>(window_, "stack", false, true);
	stack->set_radio_layer(1);

	// deep = 0.
	ttree* wml_tree = find_widget<ttree>(window_, "wml", false, true);
	wml_tree->clear();

	std::map<std::string, std::string> data;
	data.insert(std::make_pair("label", name));
	ttree_node& node = wml_tree->get_root_node().insert_node("default", data);
	node.set_child_icon("label", "misc/folder.png");

	walk_cfg_recursion(cfg, node, 0, maxdeep);
	node.unfold();
}

// building rule section
void trose::br_2_tv(const std::string& name, const terrain_builder::building_rule* rules, const int rules_size)
{
	tstack* stack = find_widget<tstack>(window_, "stack", false, true);
	stack->set_radio_layer(1);

	// deep = 0.
	ttree* wml_tree = find_widget<ttree>(window_, "wml", false, true);
	wml_tree->clear();
	
	std::stringstream ss;
	std::map<std::string, std::string> data;
	data.insert(std::make_pair("label", name));
	ttree_node& first_node = wml_tree->get_root_node().insert_node("default", data);
	first_node.set_child_icon("label", "misc/folder.png");

	for (int rule_index = 0; rule_index < rules_size; rule_index ++) {
		const terrain_builder::building_rule& r = rules[rule_index];

		ss.str("");
		ss << "#" << std::setfill('0') << std::setw(4) << rule_index << ".building_rule";
		data["label"] = ss.str();
		ttree_node& node = first_node.insert_node("default", data, twidget::npos, true);
		node.set_child_icon("label", "misc/folder.png");
		node.set_cookie(rule_index);
	}
	first_node.unfold();
}

#define null_2_space(c) ((c) >= ' ' && (c) <= 127? (c): '$')
#define null_2_null(c) ((c) >= ' '? (c): '')
static std::string format_t_terrain(const t_translation::t_terrain& t)
{
	char text[256];
	sprintf(text, "(%c%c%c%c, %c%c%c%c)", 
		null_2_space((int)(t.base & 0xff000000) >> 24), null_2_space((int)(t.base & 0xff0000) >> 16), null_2_space((int)(t.base & 0xff00) >> 8), null_2_space((int)(t.base & 0xff)),
		null_2_space((int)(t.overlay & 0xff000000) >> 24), null_2_space((int)(t.overlay & 0xff0000) >> 16), null_2_space((int)(t.overlay & 0xff00) >> 8), null_2_space((int)(t.overlay & 0xff)));

	return std::string(text);
}

static std::string t_terrain_2_str(const t_translation::t_terrain& t)
{
	std::stringstream strstr;

	if (t.base != t_translation::NO_LAYER) {
		for (int i = 24; i >= 0; i -= 8) {
			int mask = 0xff << i;
			char c = (t.base & mask) >> i;
			if (c == '\0') break;
			strstr << c;
		}
	}
	if (t.overlay != t_translation::NO_LAYER) {
		strstr << '^';
		for (int i = 24; i >= 0; i -= 8) {
			int mask = 0xff << i;
			char c = (t.overlay & mask) >> i;
			if (c == '\0') break;
			strstr << c;
		}
	}
	return strstr.str();
}

static std::string hex_t_terrain(const t_translation::t_terrain& t)
{
	char text[256];
	sprintf(text, "(0x%02x%02x%02x%02x, 0x%02x%02x%02x%02x)", 
		(t.base & 0xff000000) >> 24, (t.base & 0xff0000) >> 16, (t.base & 0xff00) >> 8, t.base & 0xff,
		(t.overlay & 0xff000000) >> 24, (t.overlay & 0xff0000) >> 16, (t.overlay & 0xff00) >> 8, t.overlay & 0xff);

	return std::string(text);
}

void trose::br_2_tv_fields(ttree_node& root, const terrain_builder::building_rule& rule)
{
	ttree_node* htvi = nullptr;
	std::stringstream ss;
	
	// constraint_set constraints;
	int at = 0;
	std::map<std::string, std::string> data;
	for (terrain_builder::constraint_set::const_iterator constraint = rule.constraints.begin(); constraint != rule.constraints.end(); ++ constraint, at ++) {
		ss.str("");
		ss << "#" << std::setfill('0') << std::setw(4) << at << ".constraint";
		data["label"] = ss.str();
		ttree_node& htvi2 = root.insert_node("default", data, twidget::npos, true);
		htvi2.set_child_icon("label", "misc/folder.png");

		//
		// terrain_constraint
		//
		// [t_translation::t_match terrain_types_match;]
		data["label"] = "t_match.terrain_types_match";
		ttree_node* htvi3 = &htvi2.insert_node("default", data, twidget::npos, true);
		htvi3->set_child_icon("label", "misc/folder.png");

		const t_translation::t_match& match = constraint->terrain_types_match;
		// t_list terrain;
		ss.str("");
		ss << "terrain=";
		for (t_translation::t_list::const_iterator itor = match.terrain.begin(); itor != match.terrain.end(); ++ itor) {
			if (itor != match.terrain.begin()) {
				ss << ",";
			}
			ss << format_t_terrain(*itor);
		}
		data["label"] = ss.str();
		htvi = &htvi3->insert_node("default", data);
		htvi->set_child_icon("label", "misc/file.png");

		// t_list mask;
		ss.str("");
		ss << "mask=";
		for (t_translation::t_list::const_iterator itor = match.mask.begin(); itor != match.mask.end(); ++ itor) {
			if (itor != match.mask.begin()) {
				ss << ",";
			}
			ss << hex_t_terrain(*itor);
		}
		data["label"] = ss.str();
		htvi = &htvi3->insert_node("default", data);
		htvi->set_child_icon("label", "misc/file.png");

		// t_list masked_terrain;
		ss.str("");
		ss << "masked_terrain=";
		for (t_translation::t_list::const_iterator itor = match.masked_terrain.begin(); itor != match.masked_terrain.end(); ++ itor) {
			if (itor != match.masked_terrain.begin()) {
				ss << ",";
			}
			ss << format_t_terrain(*itor);
		}
		data["label"] = ss.str();
		htvi = &htvi3->insert_node("default", data);
		htvi->set_child_icon("label", "misc/file.png");

		// bool has_wildcard;
		ss.str("");
		ss << "has_wildcard=" << (match.has_wildcard? "true": "false");
		data["label"] = ss.str();
		htvi = &htvi3->insert_node("default", data);
		htvi->set_child_icon("label", "misc/file.png");

		// bool is_empty;
		ss.str("");
		ss << "is_empty=" << (match.has_wildcard? "true": "false");
		data["label"] = ss.str();
		htvi = &htvi3->insert_node("default", data);
		htvi->set_child_icon("label", "misc/file.png");

		// [rule_imagelist images;]
		data["label"] = "rule_imagelist.images";
		htvi3 = &htvi2.insert_node("default", data, twidget::npos, true);
		htvi3->set_child_icon("label", "misc/folder.png");

		const terrain_builder::rule_imagelist& images = constraint->images;
		size_t image_index = 0;
		for (terrain_builder::rule_imagelist::const_iterator itor = images.begin(); itor != images.end(); ++ itor, image_index ++) {
			ss.str("");
			ss << "#" << std::setfill('0') << std::setw(2) << image_index << ".image";
			data["label"] = ss.str();
			ttree_node* htvi4 = &htvi3->insert_node("default", data, twidget::npos, true);
			htvi4->set_child_icon("label", "misc/folder.png");

			// [std::vector<rule_image_variant> variants;]
			size_t variant_index = 0;
			const std::vector<terrain_builder::rule_image_variant>& variants = itor->variants;
			for (std::vector<terrain_builder::rule_image_variant>::const_iterator variant = variants.begin(); variant != variants.end(); ++ variant, variant_index ++) {
				ss.str("");
				ss << "#" << std::setfill('0') << std::setw(2) << variant_index << ".variant";
				data["label"] = ss.str();
				ttree_node* htvi5 = &htvi4->insert_node("default", data, twidget::npos, true);
				htvi5->set_child_icon("label", "misc/folder.png");

				// std::vector< animated<image::locator> > images;
				data["label"] = "animated.images";
				ttree_node* htvi6 = &htvi5->insert_node("default", data, twidget::npos, true);
				htvi6->set_child_icon("label", "misc/folder.png");

				// std::string image_string;
				ss.str("");
				ss << "image_string=" << variant->image_string;
				data["label"] = ss.str();
				htvi = &htvi5->insert_node("default", data);
				htvi->set_child_icon("label", "misc/file.png");

				// std::string variations;
				ss.str("");
				ss << "variations=" << variant->variations;
				data["label"] = ss.str();
				htvi = &htvi5->insert_node("default", data);
				htvi->set_child_icon("label", "misc/file.png");

				// std::set<std::string> tods;
				ss.str("");
				ss << "tods=";
				for (std::set<std::string>::const_iterator tod = variant->tods.begin(); tod != variant->tods.end(); ++ tod) {
					if (tod != variant->tods.begin()) {
						ss << ",";
					}
					ss << *tod;
				}
				data["label"] = ss.str();
				htvi = &htvi5->insert_node("default", data);
				htvi->set_child_icon("label", "misc/file.png");

				// bool random_start;
				ss.str("");
				ss << "random_start=" << (variant->random_start? "true": "false");
				data["label"] = ss.str();
				htvi = &htvi5->insert_node("default", data);
				htvi->set_child_icon("label", "misc/file.png");
			}

			// int layer;
			ss.str("");
			ss << "layer=" << itor->layer;
			data["label"] = ss.str();
			htvi = &htvi4->insert_node("default", data);
			htvi->set_child_icon("label", "misc/file.png");

			// int basex, basey;
			ss.str("");
			ss << "base=(" << itor->basex << ", " << itor->basey << ")";
			data["label"] = ss.str();
			htvi = &htvi4->insert_node("default", data);
			htvi->set_child_icon("label", "misc/file.png");

			// int center_x, center_y;
			ss.str("");
			ss << "center=(" << itor->center_x << ", " << itor->center_y << ")";
			data["label"] = ss.str();
			htvi = &htvi4->insert_node("default", data);
			htvi->set_child_icon("label", "misc/file.png");

			// bool global_image;
			ss.str("");
			ss << "global_image=" << (itor->global_image? "true": "false");
			data["label"] = ss.str();
			htvi = &htvi4->insert_node("default", data);
			htvi->set_child_icon("label", "misc/file.png");
		}

		// map_location loc;
		ss.str("");
		ss << "loc=(" << constraint->loc.x << ", " << constraint->loc.y << ")";
		data["label"] = ss.str();
		htvi = &htvi2.insert_node("default", data);
		htvi->set_child_icon("label", "misc/file.png");

		// std::vector<std::string> set_flag;
		ss.str("");
		ss << "set_flag=";
		for (std::vector<std::string>::const_iterator itor = constraint->set_flag.begin(); itor != constraint->set_flag.end(); ++ itor) {
			if (itor != constraint->set_flag.begin()) {
				ss << ",";
			}
			ss << *itor;
		}
		data["label"] = ss.str();
		htvi = &htvi2.insert_node("default", data);
		htvi->set_child_icon("label", "misc/file.png");

		// std::vector<std::string> no_flag;
		ss.str("");
		ss << "no_flag=";
		for (std::vector<std::string>::const_iterator itor = constraint->no_flag.begin(); itor != constraint->no_flag.end(); ++ itor) {
			if (itor != constraint->no_flag.begin()) {
				ss << ",";
			}
			ss << *itor;
		}
		data["label"] = ss.str();
		htvi = &htvi2.insert_node("default", data);
		htvi->set_child_icon("label", "misc/file.png");

		// std::vector<std::string> has_flag;
		ss.str("");
		ss << "has_flag=";
		for (std::vector<std::string>::const_iterator itor = constraint->has_flag.begin(); itor != constraint->has_flag.end(); ++ itor) {
			if (itor != constraint->set_flag.begin()) {
				ss << ",";
			}
			ss << *itor;
		}
		data["label"] = ss.str();
		htvi = &htvi2.insert_node("default", data);
		htvi->set_child_icon("label", "misc/file.png");
	}

	// map_location location_constraints;
	ss.str("");
	ss << "location_constraints=(" << rule.location_constraints.x << ", " << rule.location_constraints.y << ")";
	data["label"] = ss.str();
	htvi = &root.insert_node("default", data);
	htvi->set_child_icon("label", "misc/file.png");

	// int probability;
	ss.str("");
	ss << "probability=" << rule.probability;
	data["label"] = ss.str();
	htvi = &root.insert_node("default", data);
	htvi->set_child_icon("label", "misc/file.png");

	// int precedence;
	ss.str("");
	ss << "precedence=" << rule.precedence;
	data["label"] = ss.str();
	htvi = &root.insert_node("default", data);
	htvi->set_child_icon("label", "misc/file.png");

	// bool local;
	ss.str("");
	ss << "local=" << (rule.local? "true": "false");
	data["label"] = ss.str();
	htvi = &root.insert_node("default", data);
	htvi->set_child_icon("label", "misc/file.png");
}

void trose::br_release_heap()
{
	if (br_rules_) {
		delete []br_rules_;
		br_rules_ = nullptr;
	}
	br_rules_size_ = 0;
}

void trose::did_wml_folded(ttree_node& node, const bool folded)
{
	if (!folded && node.empty()) {
		if (!current_wml_.second.empty()) {
			const config* cfg = reinterpret_cast<const config*>(node.cookie());
			walk_cfg_recursion(*cfg, node, 0, -1);

		} else if (node.cookie() != twidget::npos) {
			const terrain_builder::building_rule& r = br_rules_[node.cookie()];
			br_2_tv_fields(node, r);
		}
	}
}

void trose::right_click_explorer(ttree_node& node, const tpoint& coordinate)
{
	if ((int)settings::screen_width < 640 * twidget::hdpi_scale && (int)settings::screen_height < 480 * twidget::hdpi_scale) {
		return;
	}
	if (is_building()) {
		return;
	}
	if (node.get_indention_level() != 1) {
		return;
	}
	if (app_copiers.empty()) {
		return;
	}
	if (!is_apps_kit(game_config::path)) {
		return;
	}
	if (game_config::apps_src_path.empty()) {
		return;
	}
	tstack* stack = find_widget<tstack>(window_, "stack", false, true);
	stack->set_radio_layer(0);

	std::stringstream ss;
	utils::string_map symbols;
	std::vector<gui2::tmenu::titem> items, sub_items;
	
	const int app_wight = 100;
	int at = 0, app_index = 1;
	const int new_app_at = at ++;
	items.push_back(gui2::tmenu::titem(std::string(_("New app")) + "...", new_app_at));
	const int import_app_at = at ++;
	items.push_back(gui2::tmenu::titem(std::string(_("Import app")) + "...", import_app_at));

	for (std::vector<std::unique_ptr<tapp_copier> >::const_iterator it = app_copiers.begin(); it != app_copiers.end(); ++ it) {
		const tapp_copier& app = **it;
		symbols["app"] = app.app;
		sub_items.clear();
		int at2 = 0;
		if (apps_sln::can_new_dialog(app.app)) {
			sub_items.push_back(gui2::tmenu::titem(vgettext2("New dialog", symbols) + "...", app_index * app_wight + at2 ++));
			sub_items.push_back(gui2::tmenu::titem(vgettext2("New scene", symbols) + "...", app_index * app_wight + at2 ++));
			sub_items.push_back(gui2::tmenu::titem(vgettext2("Theme", symbols) + "...", app_index * app_wight + at2 ++));
		} else {
			at2 ++;
			at2 ++;
			at2 ++;
		}
		sub_items.push_back(gui2::tmenu::titem(vgettext2("Export", symbols) + "...", app_index * app_wight + at2 ++));
		sub_items.push_back(gui2::tmenu::titem(vgettext2("Edit capabilities", symbols) + "...", app_index * app_wight + at2 ++));
		sub_items.push_back(gui2::tmenu::titem(vgettext2("Copy resource to android's apk", symbols) + "...", app_index * app_wight + at2 ++));
		if (!is_reserve_app(app.app)) {
			sub_items.push_back(gui2::tmenu::titem(vgettext2("Remove", symbols) + "...", app_index * app_wight + at2 ++));
		} else {
			at2 ++;
		}

		items.push_back(gui2::tmenu::titem(app.app, sub_items));
		app_index ++;
	}

	const int tools_index = app_index;
	{
		sub_items.clear();
		int at2 = 0;
		sub_items.push_back(gui2::tmenu::titem(vgettext2("Create iOS kit", symbols) + "...", tools_index * app_wight + at2 ++));

		items.push_back(gui2::tmenu::titem(_("Tools"), sub_items));
		app_index ++;
	}

	int selected;
	{
		gui2::tmenu dlg(disp_, items, twidget::npos);
		dlg.show(disp_.video(), coordinate.x, coordinate.y);
		int retval = dlg.get_retval();
		if (dlg.get_retval() != gui2::twindow::OK) {
			return;
		}
		// absolute_draw();
		selected = dlg.selected_val();
	}
	if (selected == new_app_at) {
		tapp_capabilities capabilities(null_cfg);
		{
			gui2::tcapabilities dlg(disp_, app_copiers, twidget::npos);
			dlg.show(disp_.video());
			if (dlg.get_retval() != gui2::twindow::OK) {
				return;
			}
			capabilities = dlg.get_capabilities();
		}
		new_app(capabilities);

	} else if (selected == import_app_at) {
		std::string import_res;
		{
			gui2::tbrowse::tparam param(gui2::tbrowse::TYPE_DIR, true, directory_name2(game_config::path), _("Choose resource directory of Work Kit or App Package"));
			gui2::tbrowse dlg(disp_, param);
			dlg.set_did_result_changed(boost::bind(&trose::browse_import_res_changed, this, _1, _2));
			dlg.show(disp_.video());
			int res = dlg.get_retval();
			if (res != gui2::twindow::OK) {
				return;
			}
			import_res = param.result;
		}
		
		symbols["src"] = import_res;
		ss.str("");
		ss << vgettext2("Do you want to import app from $src?", symbols); 

		int res = gui2::show_message(disp_.video(), "", ss.str(), gui2::tmessage::yes_no_buttons);
		if (res != gui2::twindow::OK) {
			return;
		}
		absolute_draw();

		import_apps(import_res);

	} else if (selected >= app_wight && selected < ((int)app_copiers.size() + 1) * app_wight) {
		const int app_at = (selected / app_wight) - 1;
		tapp_copier& current_app = *(app_copiers[app_at].get());
		if (selected % app_wight == 0) {
			// new dialog
			std::string id;
			{
				gui2::tnew_window dlg(disp_, false, current_app);
				dlg.show(disp_.video());
				if (dlg.get_retval() != gui2::twindow::OK) {
					return;
				}
				id = dlg.get_id();
			}

			do_new_window(current_app, id, false);

		} else if (selected % app_wight == 1) {
			// new scene
			std::string id;
			bool unit_files;
			{
				gui2::tnew_window dlg(disp_, true, current_app);
				dlg.show(disp_.video());
				if (dlg.get_retval() != gui2::twindow::OK) {
					return;
				}
				id = dlg.get_id();
				unit_files = dlg.get_unit_files();
			}

			do_new_window(current_app, id, true, unit_files);

		} else if (selected % app_wight == 2) {
			// theme
			{
				gui2::tnew_theme dlg(disp_, current_app, instance->core_cfg(), *save_theme.get(), *remove_theme.get());
				dlg.show(disp_.video());
				if (dlg.get_retval() != gui2::twindow::OK) {
					return;
				}
			}
			do_normal_build(*window_, true);

		} else if (selected % app_wight == 3) {
			// export
			export_app(current_app);

		} else if (selected % app_wight == 4) {
			// edit capabilities
			gui2::tcapabilities dlg(disp_, app_copiers, app_at);
			dlg.show(disp_.video());
			if (dlg.get_retval() != gui2::twindow::OK) {
				return;
			}
			VALIDATE(!is_private_app(current_app.app), null_str);

			current_app.reset(dlg.get_capabilities());
			generate_capabilities_cfg2(game_config::path, current_app);
			do_normal_build(*window_, true);

		} else if (selected % app_wight == 5) {
			// copy resource to android's apk
			symbols["app"] = current_app.app;
			ss.str("");
			ss << vgettext2("Do you want to copy $app|'s resource to .apk?", symbols); 

			int res = gui2::show_message(disp_.video(), "", ss.str(), gui2::tmessage::yes_no_buttons);
			if (res != gui2::twindow::OK) {
				return;
			}
			absolute_draw();

			copy_android_res(current_app, false);

		} else {
/*
			{
				do_remove_dialog(current_app, "example1", false);
				return;
			}
*/
			if (is_reserve_app(current_app.app)) {
				return;
			}
			symbols["app"] = current_app.app;
			ss.str("");
			ss << vgettext2("Do you want to remove $app from work kit?", symbols); 
			int res = gui2::show_message(disp_.video(), "", ss.str(), gui2::tmessage::yes_no_buttons);
			if (res != gui2::twindow::OK) {
				return;
			}

			// maybe run Visual Studio, so remove app from apps.sln first.
			apps_sln::remove_project(current_app.app);
			{
				remover->set_app(current_app.app, current_app.tdomains);
				remover->handle(disp_);
			}

			build_on_app_changed(current_app.app, *window_, true);
		}
	} else if (selected >= tools_index * app_wight && selected < (tools_index + 1) * app_wight) {
		if (selected % app_wight == 0) {
			// Create iOS disk
			const tapp_copier* studio_copier = NULL;
			for (std::vector<std::unique_ptr<tapp_copier> >::const_iterator it = app_copiers.begin(); it != app_copiers.end(); ++ it) {
				const tapp_copier& copier = **it;
				if (copier.app == "studio") {
					studio_copier = &copier;
				}
			}

			ss.str("");
			symbols["dst"] = studio_copier->ios_kiter->alias_2_path(tios_kit::kit_alias);
			ss << vgettext2("Do you want to create iOS kit on $dst?", symbols); 
			int res = gui2::show_message(disp_.video(), "", ss.str(), gui2::tmessage::yes_no_buttons);
			if (res != gui2::twindow::OK) {
				return;
			}

			absolute_draw();
			export_ios_kit(*studio_copier);
		}
	}
}

void trose::set_working_dir(twindow& window)
{
	std::string desire_dir;
	{
		gui2::tbrowse::tparam param(gui2::tbrowse::TYPE_DIR, true, null_str, _("Choose a Working Directory to Build"));
		gui2::tbrowse dlg(disp_, param);
		dlg.show(disp_.video());
		int res = dlg.get_retval();
		if (res != gui2::twindow::OK) {
			return;
		}
		desire_dir = param.result;
	}
	if (desire_dir == editor_.working_dir()) {
		return;
	}
	if (!check_res_folder(desire_dir)) {
		std::stringstream err;
		err << desire_dir << " isn't valid res directory";
		gui2::show_message(disp_.video(), null_str, err.str());
		return;
	}

	on_change_working_dir(window, desire_dir);
}

void trose::did_draw_splitter(ttrack& widget, const SDL_Rect& draw_rect, const bool bg_drawn)
{
	SDL_Renderer* renderer = get_renderer();
	ttrack::tdraw_lock lock(renderer, widget);
	if (!bg_drawn) {
		SDL_RenderCopy(renderer, widget.background_texture().get(), nullptr, &draw_rect);
	}

	const int edge_width = 1 * twidget::hdpi_scale;
	uint32_t edge_argb = gap_argb_;
	render_rect(renderer, ::create_rect(draw_rect.x, draw_rect.y, edge_width, draw_rect.h), edge_argb);
	// render_rect(renderer, ::create_rect(draw_rect.x + edge_width, draw_rect.y, draw_rect.w - 2 * edge_width, draw_rect.h), rect_argb);
	// render_rect(renderer, ::create_rect(draw_rect.x + draw_rect.w - edge_width, draw_rect.y, edge_width, draw_rect.h), edge_argb);
}

void trose::test_gc_listbox() const
{
	std::vector<std::string> options;
	std::stringstream ss;

	std::string file_path = game_config::preferences_dir + "/data.dat";
	tfile file(file_path, GENERIC_READ, OPEN_EXISTING);
	file.read_2_data();

	const int max_times = 1000;
	char* data = (char*)malloc(4 * max_times);
	int r;
	for (int i = 0; i < max_times; i ++) {
		ss.str("");
		// ss << i << "    tscrollbar_container::calculate_scrollbar";
		ss << i << "    tscrollbar_container";
		if (file.valid()) {
			memcpy(&r, file.data + 4 * i, 4);
		} else {
			// r = rand() % 20;
			r = rand() % 10;
			memcpy(data + 4 * i, &r, 4);
		}
		// int r = 18;
		while (r-- > 0) {
			ss << "tscrollbar_container ";
		}

		options.push_back(ss.str());
	}
	if (!file.valid()) {
		tfile file2(file_path, GENERIC_WRITE, CREATE_ALWAYS);
		posix_fwrite(file2.fp, data, 4 * max_times);
	}

	gui2::tsimple_item_selector dlg(disp_, "Garbage Collection", "", options);
	dlg.show(disp_.video());
}

bool trose::compare_row(const ttoggle_panel& row1, const ttoggle_panel& row2)
{
	return true;
/*
	int i1 = (int)reinterpret_cast<long>(row1.cookie());
	int i2 = (int)reinterpret_cast<long>(row2.cookie());

	hero* h1 = &heros_[row_2_hero_.find(i1)->second];
	hero* h2 = &heros_[row_2_hero_.find(i2)->second];


	bool result = true;
	std::vector<tbutton*>& widgets = sorting_widgets_[current_page_];

		if (sorting_widget_ == widgets[0]) {
			// name
			result = utils::utf8str_compare(h1->name(), h2->name());
		} else if (sorting_widget_ == widgets[1]) {
			// side
			std::string str1, str2;
			if (h1->side_ != HEROS_INVALID_SIDE) {
				str1 =  (*teams_)[h1->side_].name();
			}
			if (h2->side_ != HEROS_INVALID_SIDE) {
				str2 =  (*teams_)[h2->side_].name();
			}
			result = utils::utf8str_compare(str1, str2);
		} 


	return ascend_? result: !result;
*/
}

void trose::do_refresh(twindow& window)
{
/*
	{
		int ii = 0;
		gui2::show_message(disp_.video(), "Hello World", "Welcome to grid layout example!", gui2::tmessage::yes_no_buttons);
		return;
	}
*/
	// test_gc_listbox();

/*
	std::vector<std::string> options;
	gui2::tsimple_item_selector dlg(disp_, "Garbage Collection", "", options);
	dlg.show(disp_.video());
*/

	fill_items(window);
}

void trose::do_normal_build(twindow& window, const bool refresh_listbox)
{
/*
	{
		tlistbox* list = find_widget<tlistbox>(&window, "items", false, true);
		list->erase_row(1);
		return;
	}
*/
/*
	{
		tlistbox* list = find_widget<tlistbox>(&window, "items", false, true);
		list->scroll_to_row(list->rows() - 2);
		return;
	}
*/
	build_msg_data_.set(build_normal, null_str);
	if (refresh_listbox) {
		fill_items(window);
	}
	do_build(window);
}

void trose::do_build(twindow& window)
{
	do_build2();
}

void trose::app_work_start()
{
	twindow& window = *window_;
	tlistbox& list = find_widget<tlistbox>(&window, "items", false);

	find_widget<tbutton>(&window, "refresh", false).set_active(false);
	find_widget<tbutton>(&window, "build", false).set_active(false);
	// find_widget<tbutton>(&window, "browse", false).set_active(false);
	list.set_active(false);

	std::vector<std::pair<teditor_::BIN_TYPE, teditor_::wml2bin_desc> >& descs = editor_.wml2bin_descs();
	int count = list.rows();
	for (int at = 0; at < count; at ++) {
		twidget& panel = list.row_panel(at);
		ttoggle_button* prefix = find_widget<ttoggle_button>(&panel, "prefix", false, true);
		descs[at].second.require_build = prefix->get_value();

		find_widget<tcontrol>(&panel, "status", false).set_label(null_str);

		// prefix->set_value(false);
	}
}

void trose::app_work_done()
{
	twindow& window = *window_;
	tbutton& refresh = find_widget<tbutton>(&window, "refresh", false);
	tbutton& build = find_widget<tbutton>(&window, "build", false);
	tlistbox& list = find_widget<tlistbox>(&window, "items", false);
	// find_widget<tbutton>(&window, "browse", false).set_active(true);

	int count = list.rows();
	for (int at = 0; at < count; at ++) {
		twidget& panel = list.row_panel(at);
		ttoggle_button* prefix = find_widget<ttoggle_button>(&panel, "prefix", false, true);
		// prefix->set_value(true);
	}

	list.set_active(true);
	refresh.set_active(true);
	build.set_active(true);
	require_set_task_bar_ = true;

	main_->Post(RTC_FROM_HERE, this, MSG_BUILD_FINISHED, NULL);
}

void trose::OnMessage(rtc::Message* msg)
{
	const int build_type = build_msg_data_.type;
	const std::string app = build_msg_data_.app;
	const std::set<std::string> apps = build_msg_data_.apps;
	twindow& window = *window_;

	build_msg_data_.set(build_normal, null_str);

	switch (msg->message_id) {
	case MSG_BUILD_FINISHED:
		if (build_type == build_export) {
			// copy res to android/res
			copy_android_res(*current_copier_, true);

			std::stringstream ss;
			utils::string_map symbols;

			symbols["src"] = current_copier_->exporter->alias_2_path(ttask::src2_alias);
			symbols["dst"] = current_copier_->exporter->alias_2_path(ttask::app_src2_alias);
			symbols["result"] = _("Success");
			ss.str("");
			ss << vgettext2("Export App Package from \"$src\" to \"$dst successfully!", symbols); 
			gui2::show_message(disp_.video(), null_str, ss.str());

		} else if (build_type == build_import) {
			const std::string src = importer->alias_2_path(ttask::app_res_alias);
			// below statement will update app_res_alias's value.
			reload_mod_configs(disp_);
			refresh_explorer_tree(*window_);

			std::stringstream ss, apps_ss;
			utils::string_map symbols;

			symbols["src"] = src;
			symbols["apps"] = utils::join(apps);
			ss.str("");
			ss << vgettext2("Import $apps from \"$src\" successfully.", symbols); 
			gui2::show_message(disp_.video(), null_str, ss.str());

		} else if (build_type == build_new) {
			reload_mod_configs(disp_);
			refresh_explorer_tree(*window_);

			std::stringstream ss;
			utils::string_map symbols;

			symbols["app"] = app;
			symbols["result"] = _("Success");
			ss.str("");
			ss << vgettext2("New $app, $result!", symbols) << "\n\n";
			ss << _("If you are runing Visual Studio and opening apps.sln, please execute \"Close Solution\", then open again.");
			gui2::show_message(disp_.video(), null_str, ss.str());

		} else if (build_type == build_remove) {
			for (std::vector<std::unique_ptr<tapp_copier> >::iterator it = app_copiers.begin(); it != app_copiers.end(); ++ it) {
				const tapp_copier& copier = **it;
				if (copier.app == app) {
					app_copiers.erase(it);
					break;
				}
			}
			reload_mod_configs(disp_);
			refresh_explorer_tree(*window_);

			std::stringstream ss;
			utils::string_map symbols;

			symbols["app"] = app;
			symbols["result"] = _("Success");
			ss.str("");
			ss << vgettext2("Remove $app, $result!", symbols) << "\n\n";
			ss << _("If you are runing Visual Studio and opening apps.sln, please execute \"Close Solution\", then open again.");
			gui2::show_message(disp_.video(), null_str, ss.str());

		} else if (build_type == build_new_dialog) {
			std::stringstream ss;
			utils::string_map symbols;

			symbols["app"] = app;
			symbols["id"] = *apps.begin();
			symbols["result"] = _("Success");
			ss.str("");
			ss << vgettext2("New dialog(id = $id) to $app successfully.", symbols) << "\n\n";
			ss << _("If you are runing Visual Studio and opening apps.sln, please execute \"Close Solution\", then open again.");
			gui2::show_message(disp_.video(), null_str, ss.str());

		} else if (build_type == build_ios_kit) {
			std::stringstream ss;
			utils::string_map symbols;

			symbols["dst"] = current_copier_->ios_kiter->alias_2_path(tios_kit::kit_alias);
			symbols["result"] = _("Success");
			ss.str("");
			ss << vgettext2("Create iOS kit on \"$dst\", $result!", symbols);
			gui2::show_message(disp_.video(), null_str, ss.str());

		} 
		break;
	}

	if (build_type == build_export || build_type == build_ios_kit) {
		on_change_working_dir(window, game_config::path);
	} else {
		fill_items(window);
	}
}

void trose::app_handle_desc(const bool started, const int at, const bool ret)
{
	tlistbox& list = find_widget<tlistbox>(window_, "items", false);
	std::stringstream ss;
	std::string str;
	std::vector<std::pair<teditor_::BIN_TYPE, teditor_::wml2bin_desc> >& descs = editor_.wml2bin_descs();

	teditor_::wml2bin_desc& desc = descs[at].second;
	twidget& panel = list.row_panel(at);

	if (!started) {
		tcontrol* filename = find_widget<tcontrol>(&panel, "filename", false, true);
		ss.str("");
		filename->set_label(desc.bin_name);
		filename->set_icon(ret? "misc/ok.png": "misc/alert.png");

		tcontrol* wml_checksum = find_widget<tcontrol>(&panel, "wml_checksum", false, true);
		ss.str("");
		ss << "(" << desc.wml_nfiles << ", " << desc.wml_sum_size << ", " << desc.wml_modified << ")";
		wml_checksum->set_label(ss.str());

		tcontrol* bin_checksum = find_widget<tcontrol>(&panel, "bin_checksum", false, true);
		desc.refresh_checksum(editor_.working_dir());
		ss.str("");
		ss << "(" << desc.bin_nfiles << ", " << desc.bin_sum_size << ", " << desc.bin_modified << ")";
		bin_checksum->set_label(ss.str());
	}
	tcontrol* status = find_widget<tcontrol>(&panel, "status", false, true);
	if (started) {
		str = "misc/operating.png";
	} else {
		str = ret? "misc/success.png": "misc/fail.png";
	}
	status->set_label(str);
}

void trose::generate_gui_app_main_cfg(const std::string& res_path, const std::set<std::string>& apps) const
{
	// if necessary, generate <apps-res>/data/gui/app/app-xxx/_main.cfg
	std::stringstream ss, fp_ss;

	for (std::set<std::string>::const_iterator it = apps.begin(); it != apps.end(); ++ it) {
		const std::string& app = *it;
		fp_ss.str("");
		fp_ss << "#\n";
		fp_ss << "# NOTE: it is generated by rose studio, don't edit yourself.\n";
		fp_ss << "#\n";
		fp_ss << "\n";

		// {gui/app-xxx/widget/}
		// {gui/app-xxx/window/}
		// {gui/app-xxx/scene/}
		fp_ss << "{gui/" << game_config::generate_app_dir(app) << "/widget/}\n";
		fp_ss << "{gui/" << game_config::generate_app_dir(app) << "/window/}\n";
		fp_ss << "{gui/" << game_config::generate_app_dir(app) << "/scene/}";

		ss.str("");
		ss << res_path << "/data/gui/";
		ss << game_config::generate_app_dir(app);
		ss << "/_main.cfg";
		if (file_exists(ss.str())) {
			tfile file2(ss.str(), GENERIC_READ, OPEN_EXISTING);
			int fsize = file2.read_2_data();
			if (fsize == fp_ss.str().size() && !memcmp(fp_ss.str().c_str(), file2.data, fsize)) {
				continue;
			}
		}

		tfile fp(ss.str(), GENERIC_WRITE, CREATE_ALWAYS);
		if (!fp.valid()) {
			return;
		}
		posix_fwrite(fp.fp, fp_ss.str().c_str(), fp_ss.str().length());
	}
}

void trose::generate_app_cfg(const std::string& res_path, const std::set<std::string>& apps) const
{
	std::string base_dir, app_dir;
	std::stringstream fp_ss;
	
	enum {app_cfg_data, app_cfg_gui, app_count};

	for (int type = app_cfg_data; type < app_count; type ++) {
		fp_ss.str("");
		fp_ss << "#\n";
		fp_ss << "# NOTE: it is generated by rose studio, don't edit yourself.\n";
		fp_ss << "#\n";
		fp_ss << "\n";
		for (std::set<std::string>::const_iterator it = apps.begin(); it != apps.end(); ++ it) {
			const std::string& app = *it;
			const std::string app_short_dir = std::string("app-") + app;
			if (it != apps.begin()) {
				fp_ss << "\n";
				if (type == app_cfg_data) {
					fp_ss << "\n";
				}
			}
			fp_ss << "{";
			if (type == app_cfg_data) {
				base_dir = res_path + "/data";
				fp_ss << app_short_dir << "/_main.cfg}\n";
				fp_ss << "[capabilities]\n";
				fp_ss << "\tapp = " << app << "\n";
				fp_ss << "\t{" << app_short_dir << "/capabilities.cfg" << "}\n";
				fp_ss << "[/capabilities]";

			} else if (type == app_cfg_gui) {
				base_dir = res_path + "/data/gui";
				fp_ss << "gui/" << app_short_dir << "/_main.cfg";
				fp_ss << "}";
			} else {
				VALIDATE(false, null_str);
			}
		}

		// if _main.cfg doesn't exist, create a empty app.cfg. it is necessary for arthiture.
		std::string file = base_dir + "/app.cfg";

		if (file_exists(file)) {
			tfile file2(file, GENERIC_READ, OPEN_EXISTING);
			int fsize = file2.read_2_data();
			if (fsize == fp_ss.str().size() && !memcmp(fp_ss.str().c_str(), file2.data, fsize)) {
				continue;
			}
		}

		tfile fp(file, GENERIC_WRITE, CREATE_ALWAYS);
		posix_fwrite(fp.fp, fp_ss.str().c_str(), fp_ss.str().length());
	}
}

void trose::generate_capabilities_cfg(const std::string& res_path) const
{
	for (std::vector<std::unique_ptr<tapp_copier> >::const_iterator it = app_copiers.begin(); it != app_copiers.end(); ++ it) {
		tapp_copier& copier = **it;
		generate_capabilities_cfg2(res_path, copier);		
	}
}

void trose::generate_capabilities_cfg2(const std::string& res_path, const tapp_capabilities& copier) const
{
	// if necessary, generate <apps-res>/data/app-xxx/capabilities.cfg
	std::stringstream ss, fp_ss;

	const std::string& app = copier.app;
	copier.generate2(fp_ss, null_str);

	ss.str("");
	ss << res_path << "/data/";
	ss << game_config::generate_app_dir(app);
	ss << "/capabilities.cfg";
	if (file_exists(ss.str())) {
		tfile file2(ss.str(), GENERIC_READ, OPEN_EXISTING);
		int fsize = file2.read_2_data();
		if (fsize == fp_ss.str().size() && !memcmp(fp_ss.str().c_str(), file2.data, fsize)) {
			return;
		}
	}

	tfile fp(ss.str(), GENERIC_WRITE, CREATE_ALWAYS);
	if (!fp.valid()) {
		return;
	}
	posix_fwrite(fp.fp, fp_ss.str().c_str(), fp_ss.str().length());
}

bool trose::collect_app(const std::string& dir, const SDL_dirent2* dirent, std::set<std::string>& apps)
{
	bool isdir = SDL_DIRENT_DIR(dirent->mode);
	if (isdir) {
		const std::string app = game_config::extract_app_from_app_dir(dirent->name);
		if (!app.empty()) {
			apps.insert(app);
		}
	}

	return true;
}

bool trose::collect_app2(const std::string& dir, const SDL_dirent2* dirent, std::vector<std::unique_ptr<tapp_copier> >& app_copiers)
{
	bool isdir = SDL_DIRENT_DIR(dirent->mode);
	if (isdir) {
		const std::string app = game_config::extract_app_from_app_dir(dirent->name);
		if (!app.empty()) {
			const config& cfg = instance->core_cfg().find_child("capabilities", "app", app);
			app_copiers.push_back(std::unique_ptr<tapp_copier>(new tapp_copier(cfg? cfg: null_cfg, app)));
		}
	}

	return true;
}

void trose::reload_mod_configs(display& disp)
{
	generate_cfg.clear();
	app_copiers.clear();
	tdomains.clear();

	if (check_res_folder(game_config::path)) {
		game_config::config_cache_transaction transaction;
		game_config::config_cache& cache = game_config::config_cache::instance();
		cache.clear_defines();

		cache.get_config(game_config::absolute_path + "/generate.cfg", generate_cfg);
	}

	if (generate_cfg.empty()) {
		// on iOS/Android, there is no generate.cfg.
		return;
	}

	::walk_dir(game_config::path, false, boost::bind(
				&trose::collect_app2
				, this
				, _1, _2, boost::ref(app_copiers)));

	const config* export_cfg = NULL, *studio_extra_export_cfg = NULL, *android_res_cfg = NULL, *ios_kit_cfg = NULL;
	BOOST_FOREACH (const config& c, generate_cfg.child_range("generate")) {
		const std::string& type = c["type"].str();
		if (type == "new_app") {
			newer = ttask::create_task<tnewer>(c, "new", NULL);

		} else if (type == "import") {
			importer = ttask::create_task<timporter>(c, "import", NULL);

		} else if (type == "new_dialog") {
			new_dialog = ttask::create_task<tnew_dialog>(c, "new_dialog", NULL);

		} else if (type == "new_scene") {
			new_scene = ttask::create_task<tnew_scene>(c, "new_scene", NULL);

		} else if (type == "remove_app") {
			remover = ttask::create_task<tremover>(c, "remove", NULL);

		} else if (type == "validate_res") {
			validater = ttask::create_task<tvalidater>(c, "validate", NULL);

		} else if (type == "save_theme") {
			save_theme = ttask::create_task<tsave_theme>(c, "save_theme", NULL);

		} else if (type == "remove_theme") {
			remove_theme = ttask::create_task<tremove_theme>(c, "remove_theme", NULL);

		} else if (type == "export") {
			export_cfg = &c;

		} else if (type == "studio_extra_export") {
			studio_extra_export_cfg = &c;

		} else if (type == "android_res") {
			android_res_cfg = &c;

		} else if (type == "ios_kit") {
			ios_kit_cfg = &c;
		}
	}

	VALIDATE(export_cfg && studio_extra_export_cfg && android_res_cfg, null_str);

	std::set<std::string> apps;
	tdomains.insert(std::make_pair("rose-lib", null_str));
	tdomains.insert(std::make_pair("editor-lib", null_str));

	for (std::vector<std::unique_ptr<tapp_copier> >::const_iterator it = app_copiers.begin(); it != app_copiers.end(); ++ it) {
		tapp_copier& copier = **it;

		copier.exporter = ttask::create_task<texporter>(*export_cfg, "export", &copier);
		copier.android_res_copier = ttask::create_task<tandroid_res>(*android_res_cfg, "android_res", &copier);
		if (is_studio_app(copier.app)) {
			copier.studio_extra_exporter = ttask::create_task<tstudio_extra_exporter>(*studio_extra_export_cfg, "export", &copier);
			copier.ios_kiter = ttask::create_task<tios_kit>(*ios_kit_cfg, "export", &copier);
		}

		for (std::set<std::string>::const_iterator it = copier.tdomains.begin(); it != copier.tdomains.end(); ++ it) {
			tdomains.insert(std::make_pair(*it, copier.app));
		}
		apps.insert(copier.app);
	}

	generate_capabilities_cfg(game_config::path);
	VALIDATE(validater.get(), "Must define validate_res.");
	validater_res(apps);
}

void trose::validater_res(const std::set<std::string>& apps)
{
	if (!is_apps_kit(game_config::path)) {
		return;
	}

	for (std::set<std::string>::const_iterator it = apps.begin(); it != apps.end(); ++ it) {
		const std::string& app = *it;
		validater->set_app(app);
		validater->handle(disp_);
	}
	generate_gui_app_main_cfg(game_config::path, apps);
	generate_app_cfg(game_config::path, apps);
}

} // namespace gui2

