#include "platform/theseus_config.h"

#include <algorithm>
#include <charconv>
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

  if ((section == "display" || legacy) && key == "width") int_value(c.width);
  else if ((section == "display" || legacy) && key == "height") int_value(c.height);
  else if ((section == "display" || legacy) && key == "fullscreen") bool_value(c.fullscreen);
  else if ((section == "display" || legacy) && key == "vsync") bool_value(c.vsync);
  else if ((section == "graphics" || legacy) && key == "renderer") int_value(c.renderer);
  else if ((section == "graphics" || legacy) && key == "adapter") int_value(c.adapter);
  else if ((section == "graphics" || legacy) && key == "asyncshaders") bool_value(c.async_shaders);
  else if ((section == "input" || legacy) && key == "keyboardmouse") bool_value(c.mnk);
  else if ((section == "input" || legacy) && key == "inputbackend") int_value(c.input_backend);
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
      << "Fullscreen=" << (c.fullscreen ? 1 : 0) << "\n"
      << "VSync=" << (c.vsync ? 1 : 0) << "\n\n"
      << "[Graphics]\n"
      << "Renderer=" << c.renderer << "\n"
      << "Adapter=" << c.adapter << "\n"
      << "AsyncShaders=" << (c.async_shaders ? 1 : 0) << "\n\n"
      << "[Input]\n"
      << "KeyboardMouse=" << (c.mnk ? 1 : 0) << "\n"
      << "InputBackend=" << c.input_backend << "\n\n"
      << "[Language]\n"
      << "Language=" << c.language << "\n"
      << "Country=" << c.country << "\n\n"
      << "[Launcher]\n"
      << "ShowAtStartup=" << (c.show_at_startup ? 1 : 0) << "\n";

  return out.good();
}

}  // namespace theseus
