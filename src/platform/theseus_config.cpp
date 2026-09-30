#include "platform/theseus_config.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <string>
#include <string_view>

namespace theseus {
namespace {

std::string Trim(std::string value) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(),
              value.end());
  return value;
}

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool ParseInt(std::string_view value, int& out) {
  const auto* begin = value.data();
  const auto* end = begin + value.size();
  int parsed = 0;
  auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc{} || ptr != end) {
    return false;
  }
  out = parsed;
  return true;
}

bool ParseDouble(std::string value, double& out) {
  value = Trim(std::move(value));
  if (value.empty()) {
    return false;
  }
  char* end = nullptr;
  const double parsed = std::strtod(value.c_str(), &end);
  if (!end || *end != '\0') {
    return false;
  }
  out = parsed;
  return true;
}

bool ParseBool(std::string value, bool& out) {
  value = Lower(Trim(std::move(value)));
  if (value == "1" || value == "true" || value == "yes" || value == "on") {
    out = true;
    return true;
  }
  if (value == "0" || value == "false" || value == "no" || value == "off") {
    out = false;
    return true;
  }
  return false;
}

void ApplyValue(Config& c, std::string section, std::string key,
                const std::string& value) {
  section = Lower(Trim(std::move(section)));
  key = Lower(Trim(std::move(key)));

  // Legacy RUN04 Launcher.ini used one [Settings] section. Accepting it makes
  // migration to UserData/Config/Asura.ini lossless.
  const bool legacy = section == "settings";

  auto int_value = [&](int& target) {
    int parsed = target;
    if (ParseInt(value, parsed)) {
      target = parsed;
    }
  };
  auto bool_value = [&](bool& target) {
    bool parsed = target;
    if (ParseBool(value, parsed)) {
      target = parsed;
    }
  };
  auto double_value = [&](double& target) {
    double parsed = target;
    if (ParseDouble(value, parsed)) {
      target = parsed;
    }
  };
  auto string_value = [&](std::string& target) {
    const auto parsed = Trim(value);
    if (!parsed.empty()) {
      target = parsed;
    }
  };

  if ((section == "display" || legacy) && key == "width") int_value(c.width);
  else if ((section == "display" || legacy) && key == "height") int_value(c.height);
  else if ((section == "display" || legacy) && key == "monitor") int_value(c.monitor);
  else if ((section == "display" || legacy) && key == "renderscalepreset") int_value(c.render_resolution);
  else if ((section == "display" || legacy) && key == "renderresolution") {
    int legacy_render_resolution = 0;
    int_value(legacy_render_resolution);
    switch (legacy_render_resolution) {
      case 4: c.render_resolution = 2; break;  // Old 4K -> true 3x/4K.
      case 1:
      case 2: c.render_resolution = 1; break;  // Old 1080p/1440p -> true 2x.
      case 0:
      case 3:
      default: c.render_resolution = 0; break; // 720p/Native -> safe original.
    }
  }
  else if ((section == "display" || legacy) && key == "fullscreen") bool_value(c.fullscreen);
  else if ((section == "display" || legacy) && key == "vsync") bool_value(c.vsync);
  else if ((section == "graphics" || legacy) && key == "renderer") int_value(c.renderer);
  else if ((section == "graphics" || legacy) && key == "adapter") int_value(c.adapter);
  else if ((section == "graphics" || legacy) && key == "asyncshaders") bool_value(c.async_shaders);
  else if ((section == "input" || legacy) && key == "keyboardmouse") bool_value(c.mnk);
  else if ((section == "input" || legacy) && key == "inputbackend") int_value(c.input_backend);
  else if ((section == "input" || legacy) && key == "mouselook") bool_value(c.mouse_look);
  else if ((section == "input" || legacy) && key == "mousesensitivity") double_value(c.mouse_sensitivity);
  else if ((section == "input" || legacy) && key == "hidecursoringame") bool_value(c.hide_cursor_in_game);
  else if (section == "keybinds" && key == "a") string_value(c.keybinds.a);
  else if (section == "keybinds" && key == "b") string_value(c.keybinds.b);
  else if (section == "keybinds" && key == "x") string_value(c.keybinds.x);
  else if (section == "keybinds" && key == "y") string_value(c.keybinds.y);
  else if (section == "keybinds" && key == "lefttrigger") string_value(c.keybinds.left_trigger);
  else if (section == "keybinds" && key == "righttrigger") string_value(c.keybinds.right_trigger);
  else if (section == "keybinds" && key == "leftshoulder") string_value(c.keybinds.left_shoulder);
  else if (section == "keybinds" && key == "rightshoulder") string_value(c.keybinds.right_shoulder);
  else if (section == "keybinds" && key == "leftstickup") string_value(c.keybinds.left_stick_up);
  else if (section == "keybinds" && key == "leftstickdown") string_value(c.keybinds.left_stick_down);
  else if (section == "keybinds" && key == "leftstickleft") string_value(c.keybinds.left_stick_left);
  else if (section == "keybinds" && key == "leftstickright") string_value(c.keybinds.left_stick_right);
  else if (section == "keybinds" && key == "leftstickpress") string_value(c.keybinds.left_stick_press);
  else if (section == "keybinds" && key == "rightstickup") string_value(c.keybinds.right_stick_up);
  else if (section == "keybinds" && key == "rightstickdown") string_value(c.keybinds.right_stick_down);
  else if (section == "keybinds" && key == "rightstickleft") string_value(c.keybinds.right_stick_left);
  else if (section == "keybinds" && key == "rightstickright") string_value(c.keybinds.right_stick_right);
  else if (section == "keybinds" && key == "rightstickpress") string_value(c.keybinds.right_stick_press);
  else if (section == "keybinds" && key == "dpadup") string_value(c.keybinds.dpad_up);
  else if (section == "keybinds" && key == "dpaddown") string_value(c.keybinds.dpad_down);
  else if (section == "keybinds" && key == "dpadleft") string_value(c.keybinds.dpad_left);
  else if (section == "keybinds" && key == "dpadright") string_value(c.keybinds.dpad_right);
  else if (section == "keybinds" && key == "back") string_value(c.keybinds.back);
  else if (section == "keybinds" && key == "start") string_value(c.keybinds.start);
  else if ((section == "audio" || legacy) && key == "mute") bool_value(c.audio_mute);
  else if ((section == "audio" || legacy) && key == "queuedframes") int_value(c.audio_queued_frames);
  else if ((section == "language" || legacy) && key == "language") int_value(c.language);
  else if ((section == "language" || legacy) && key == "country") int_value(c.country);
  else if ((section == "launcher" || legacy) && key == "showatstartup") bool_value(c.show_at_startup);
}

}  // namespace

bool LoadConfig(const std::filesystem::path& path, Config& config) {
  std::ifstream in(path);
  if (!in.is_open()) {
    return false;
  }

  std::string section;
  std::string line;
  while (std::getline(in, line)) {
    line = Trim(std::move(line));
    if (line.empty() || line.starts_with('#') || line.starts_with(';')) {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      section = line.substr(1, line.size() - 2);
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    ApplyValue(config, section, line.substr(0, eq), Trim(line.substr(eq + 1)));
  }

  return true;
}

bool SaveConfig(const std::filesystem::path& path, const Config& c) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);

  std::ofstream out(path, std::ios::trunc);
  if (!out.is_open()) {
    return false;
  }

  out << "# Asura's Wrath - Theseus PC configuration\n"
      << "# This file belongs to Theseus, not to a specific runtime backend.\n\n"
      << "[Display]\n"
      << "Width=" << c.width << "\n"
      << "Height=" << c.height << "\n"
      << "Monitor=" << c.monitor << "\n"
      << "RenderScalePreset=" << c.render_resolution << "\n"
      << "Fullscreen=" << (c.fullscreen ? 1 : 0) << "\n"
      << "VSync=" << (c.vsync ? 1 : 0) << "\n\n"
      << "[Graphics]\n"
      << "Renderer=" << c.renderer << "\n"
      << "Adapter=" << c.adapter << "\n"
      << "AsyncShaders=" << (c.async_shaders ? 1 : 0) << "\n\n"
      << "[Input]\n"
      << "KeyboardMouse=" << (c.mnk ? 1 : 0) << "\n"
      << "InputBackend=" << c.input_backend << "\n"
      << "MouseLook=" << (c.mouse_look ? 1 : 0) << "\n"
      << "MouseSensitivity=" << c.mouse_sensitivity << "\n"
      << "HideCursorInGame=" << (c.hide_cursor_in_game ? 1 : 0) << "\n\n"
      << "[Keybinds]\n"
      << "A=" << c.keybinds.a << "\n"
      << "B=" << c.keybinds.b << "\n"
      << "X=" << c.keybinds.x << "\n"
      << "Y=" << c.keybinds.y << "\n"
      << "LeftTrigger=" << c.keybinds.left_trigger << "\n"
      << "RightTrigger=" << c.keybinds.right_trigger << "\n"
      << "LeftShoulder=" << c.keybinds.left_shoulder << "\n"
      << "RightShoulder=" << c.keybinds.right_shoulder << "\n"
      << "LeftStickUp=" << c.keybinds.left_stick_up << "\n"
      << "LeftStickDown=" << c.keybinds.left_stick_down << "\n"
      << "LeftStickLeft=" << c.keybinds.left_stick_left << "\n"
      << "LeftStickRight=" << c.keybinds.left_stick_right << "\n"
      << "LeftStickPress=" << c.keybinds.left_stick_press << "\n"
      << "RightStickUp=" << c.keybinds.right_stick_up << "\n"
      << "RightStickDown=" << c.keybinds.right_stick_down << "\n"
      << "RightStickLeft=" << c.keybinds.right_stick_left << "\n"
      << "RightStickRight=" << c.keybinds.right_stick_right << "\n"
      << "RightStickPress=" << c.keybinds.right_stick_press << "\n"
      << "DPadUp=" << c.keybinds.dpad_up << "\n"
      << "DPadDown=" << c.keybinds.dpad_down << "\n"
      << "DPadLeft=" << c.keybinds.dpad_left << "\n"
      << "DPadRight=" << c.keybinds.dpad_right << "\n"
      << "Back=" << c.keybinds.back << "\n"
      << "Start=" << c.keybinds.start << "\n\n"
      << "[Audio]\n"
      << "Mute=" << (c.audio_mute ? 1 : 0) << "\n"
      << "QueuedFrames=" << c.audio_queued_frames << "\n\n"
      << "[Language]\n"
      << "Language=" << c.language << "\n"
      << "Country=" << c.country << "\n\n"
      << "[Launcher]\n"
      << "ShowAtStartup=" << (c.show_at_startup ? 1 : 0) << "\n";

  return out.good();
}

}  // namespace theseus
