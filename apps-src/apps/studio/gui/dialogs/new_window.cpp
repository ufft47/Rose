#define GETTEXT_DOMAIN "studio-lib"

#include "gui/dialogs/new_window.hpp"

#include "gui/widgets/settings.hpp"
#include "gui/widgets/label.hpp"
#include "gui/widgets/button.hpp"
#include "gui/widgets/text_box.hpp"
#include "gui/widgets/window.hpp"
#include "gettext.hpp"
#include "formula_string_utils.hpp"
#include "game_config.hpp"

namespace gui2 {

REGISTER_DIALOG(studio, new_window)

tnew_window::tnew_window(display& disp, bool scene, const tapp_copier& app)
	: disp_(disp)
	, scene_(scene)
	, app_(app)
	, id_(nullptr)
	, unit_files_(false)
{
	if (scene_) {
		unit_files_ = is_generate_unit_files();
	}
}

void tnew_window::pre_show(CVideo& video, twindow& window)
{
	window.set_canvas_variable("border", variant("default-border"));

	std::stringstream ss;
	utils::string_map symbols;

	symbols["app"] = app_.app;
	symbols["window"] = scene_? _("scene"): _("dialog");

	ss.str("");
	ss << vgettext2("New $window to $app", symbols); 
	find_widget<tlabel>(&window, "title", false).set_label(ss.str());

	id_ = find_widget<ttext_box>(&window, "id", false, true);
	id_->set_did_text_changed(boost::bind(&tnew_window::did_id_changed, this, boost::ref(window), _1));
	id_->set_placeholder("new id");
	window.keyboard_capture(id_);

	find_widget<tbutton>(&window, "ok", false).set_label(_("Create"));
	find_widget<tbutton>(&window, "ok", false).set_active(false);

	refresh_file_tip(window, null_str);
}

void tnew_window::post_show(twindow& window)
{
	VALIDATE(id_, null_str);
	ret_id_ = id_->label();
}

std::string tnew_window::calculate_res_base_path() const
{
	std::stringstream ss;
	ss << game_config::path + "/data/gui/" + game_config::generate_app_dir(app_.app) << "/";
	ss << (scene_? "scene": "window");

	return ss.str();
}

std::string tnew_window::calculate_src_base_path() const
{
	VALIDATE(!game_config::apps_src_path.empty() && scene_, null_str);

	return game_config::apps_src_path + "/apps/" + app_.app;
}

std::string tnew_window::calculate_src_gui_base_path() const
{
	VALIDATE(!game_config::apps_src_path.empty(), null_str);

	return game_config::apps_src_path + "/apps/" + app_.app + "/gui/dialogs";
}

std::string tnew_window::calculate_cfg_path(const std::string& id) const
{
	std::stringstream ss;

	ss << calculate_res_base_path() + "/";
	if (!id.empty()) {
		ss << id << (scene_? "_scene.cfg": ".cfg");
	}

	return ss.str();
}

std::vector<std::string> tnew_window::calculate_src_files(const std::string& id) const
{
	std::stringstream ss;

	std::vector<std::string> ret;
	if (scene_) {
		// mkwin_controller.cpp
		ss.str("");
		ss << calculate_src_base_path() + "/";
		if (!id.empty()) {
			ss << id << "_controller.cpp";
		}
		ret.push_back(ss.str());

		// mkwin_controller.hpp
		ss.str("");
		ss << calculate_src_base_path() + "/";
		if (!id.empty()) {
			ss << id << "_controller.hpp";
		}
		ret.push_back(ss.str());

		// mkwin_display.cpp
		ss.str("");
		ss << calculate_src_base_path() + "/";
		if (!id.empty()) {
			ss << id << "_display.cpp";
		}
		ret.push_back(ss.str());

		// mkwin_display.hpp
		ss.str("");
		ss << calculate_src_base_path() + "/";
		if (!id.empty()) {
			ss << id << "_display.hpp";
		}
		ret.push_back(ss.str());

		// gui/dialogs/mkwin_scene.cpp
		ss.str("");
		ss << calculate_src_gui_base_path() + "/";
		if (!id.empty()) {
			ss << id << "_scene.cpp";
		}
		ret.push_back(ss.str());

		// gui/dialogs/mkwin_scene.hpp
		ss.str("");
		ss << calculate_src_gui_base_path() + "/";
		if (!id.empty()) {
			ss << id << "_scene.hpp";
		}
		ret.push_back(ss.str());

		if (unit_files_) {
			std::vector<std::string> ret2 = calculate_src_unit_files();
			ret.insert(ret.end(), ret2.begin(), ret2.end());
		}

	} else {
		ss.str("");
		ss << calculate_src_gui_base_path() + "/";
		if (!id.empty()) {
			ss << id << ".cpp";
		}
		ret.push_back(ss.str());

		ss.str("");
		ss << calculate_src_gui_base_path() + "/";
		if (!id.empty()) {
			ss << id << ".hpp";
		}
		ret.push_back(ss.str());
	}

	return ret;
}

std::vector<std::string> tnew_window::calculate_src_unit_files() const
{
	std::vector<std::string> files;
	files.push_back(calculate_src_base_path() + "/unit_map.cpp");
	files.push_back(calculate_src_base_path() + "/unit_map.hpp");
	files.push_back(calculate_src_base_path() + "/unit.cpp");
	files.push_back(calculate_src_base_path() + "/unit.hpp");

	return files;
}

bool tnew_window::is_generate_unit_files() const
{
	std::vector<std::string> files = calculate_src_unit_files();

	for (std::vector<std::string>::const_iterator it = files.begin(); it != files.end(); ++ it) {
		const std::string& file = *it;
		if (SDL_IsFile(file.c_str())) {
			return false;
		}
	}
	return true;
}

void tnew_window::refresh_file_tip(twindow& window, const std::string& id) const
{
	std::stringstream ss;

	tlabel& res_file = find_widget<tlabel>(&window, "res_tip", false);
	res_file.set_label(calculate_cfg_path(id));

	const std::string src_base_path = calculate_src_gui_base_path();
	ss.str("");
	std::vector<std::string> src_files = calculate_src_files(id);
	ss << utils::join(src_files, "\n");
	if (scene_) {
		ss << "\n\n";
		if (!unit_files_) {
			ss << _("Don't create unit_map.cpp, unit_map.hpp, unit.cpp and unit.hpp.");
		}

	}

	tlabel& src_file = find_widget<tlabel>(&window, "src_tip", false);
	src_file.set_label(ss.str());

	window.invalidate_layout();
}

void tnew_window::did_id_changed(twindow& window, ttext_box& widget)
{
	const std::string id = widget.label();
	int size = id.size();
	bool active = utils::isvalid_id(id, true, 3, 24);
	if (active) {
		active = id.find(' ') == std::string::npos;
	}
	if (active) {
		std::vector<std::string> mids;
		mids.push_back("simple");
		mids.push_back("studio");
		for (std::vector<std::string>::const_iterator it = mids.begin(); it != mids.end(); ++ it) {
			if (id.find(*it) != std::string::npos) {
				active = false;
				break;
			}
		}
	}
	if (active) {
		// relative files must not exist.
		std::vector<std::string> files = calculate_src_files(id);
		files.push_back(calculate_cfg_path(id));
		
		for (std::vector<std::string>::const_iterator it = files.begin(); it != files.end(); ++ it) {
			const std::string& file = *it;
			if (SDL_IsFile(file.c_str())) {
				active = false;
				break;
			}
		}
	}
	if (active) {
		// must not registed window.
		const std::map<std::string, twindow_builder>& window_types = gui.window_types;

		const std::string key = utils::generate_app_prefix_id(app_.app, id);
		for (std::map<std::string, twindow_builder>::const_iterator it = window_types.begin(); it != window_types.end(); ++ it) {
			if (it->first == key) {
				active = false;
				break;
			}
		}
	}

	refresh_file_tip(window, id);
	find_widget<tbutton>(&window, "ok", false).set_active(active);
}

} // namespace gui2

