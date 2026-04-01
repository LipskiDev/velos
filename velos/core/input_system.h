#pragma once

#include "core/input_types.h"
#include <array>
#include <vector>

namespace Velos {

struct ButtonState {
  bool down = false;
  bool pressed = false;
  bool released = false;
};

class InputSystem {
public:
  void BeginFrame();
  void ProcessEvent(const InputEvent &event);

  bool IsKeyDown(Key key) const;
  bool WasKeyPressed(Key key) const;
  bool WasKeyReleased(Key key) const;

  bool IsMouseDown(MouseButton button) const;
  bool WasMousePressed(MouseButton button) const;
  bool WasMouseReleased(MouseButton button) const;

  float GetMouseX() const;
  float GetMouseY() const;
  float GetMouseDeltaX() const;
  float GetMouseDeltaY() const;

  float GetScrollX() const;
  float GetScrollY() const;

  const std::vector<uint32_t> &GetTextInput() const;
  const std::vector<InputEvent> &GetEvents() const;

private:
  std::array<ButtonState, static_cast<size_t>(Key::Count)> keys_{};
  std::array<ButtonState, static_cast<size_t>(MouseButton::Count)>
      mouseButtons_{};

  float mouseX_ = 0.0f;
  float mouseY_ = 0.0f;
  float mouseDeltaX_ = 0.0f;
  float mouseDeltaY_ = 0.0f;
  float scrollX_ = 0.0f;
  float scrollY_ = 0.0f;

  std::vector<uint32_t> textInput_;
  std::vector<InputEvent> events_;
};

} // namespace Velos
