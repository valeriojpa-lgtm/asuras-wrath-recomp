#pragma once

#include <string>

namespace theseus {

enum class InputBackend {
  kSDL = 0,
  kXInput = 1,
};

struct InputBindings {
  std::string a = "Semicolon,Space";
  std::string b = "Quote,Backspace";
  std::string x = "L";
  std::string y = "P";
  std::string left_trigger = "Q,I";
  std::string right_trigger = "E,O";
  std::string left_shoulder = "1";
  std::string right_shoulder = "3";
  std::string left_stick_up = "W";
  std::string left_stick_down = "S";
  std::string left_stick_left = "A";
  std::string left_stick_right = "D";
  std::string left_stick_press = "F";
  std::string right_stick_up = "Up";
  std::string right_stick_down = "Down";
  std::string right_stick_left = "Left";
  std::string right_stick_right = "Right";
  std::string right_stick_press = "K";
  std::string dpad_up = "Shift+Up";
  std::string dpad_down = "Shift+Down";
  std::string dpad_left = "Shift+Left";
  std::string dpad_right = "Shift+Right";
  std::string back = "Z,Tab";
  std::string start = "X,Return";
};

struct InputPolicyState {
  InputBackend backend = InputBackend::kSDL;
  bool keyboard_mouse = true;
  bool mouse_look = false;
  double mouse_sensitivity = 1.0;
  bool hide_cursor_in_game = true;
  InputBindings bindings{};
};

class NativeInputPolicy final {
 public:
  void Configure(const InputPolicyState& state) { state_ = state; }

  [[nodiscard]] const InputPolicyState& state() const noexcept {
    return state_;
  }

 private:
  InputPolicyState state_{};
};

}  // namespace theseus
