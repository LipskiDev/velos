#include "core/input_system.h"

namespace Velos {

void InputSystem::BeginFrame() {
  for (auto &key : keys_) {
    key.pressed = false;
    key.released = false;
  }

  for (auto &button : mouseButtons_) {
    button.pressed = false;
    button.released = false;
  }

  mouseDeltaX_ = 0.0f;
  mouseDeltaY_ = 0.0f;
  scrollX_ = 0.0f;
  scrollY_ = 0.0f;

  textInput_.clear();
  events_.clear();
}

void InputSystem::ProcessEvent(const InputEvent &event) {
  events_.push_back(event);

  switch (event.type) {
  case InputEventType::KeyDown: {
    auto &state = keys_[static_cast<size_t>(event.key.key)];
    if (!state.down)
      state.pressed = true;
    state.down = true;
    break;
  }
  case InputEventType::KeyUp: {
    auto &state = keys_[static_cast<size_t>(event.key.key)];
    state.down = false;
    state.released = true;
    break;
  }
  case InputEventType::MouseButtonDown: {
    auto &state = mouseButtons_[static_cast<size_t>(event.mouseButton.button)];
    if (!state.down)
      state.pressed = true;
    state.down = true;
    break;
  }
  case InputEventType::MouseButtonUp: {
    auto &state = mouseButtons_[static_cast<size_t>(event.mouseButton.button)];
    state.down = false;
    state.released = true;
    break;
  }
  case InputEventType::MouseMove: {
    mouseDeltaX_ += event.mouseMove.x - mouseX_;
    mouseDeltaY_ += event.mouseMove.y - mouseY_;
    mouseX_ = event.mouseMove.x;
    mouseY_ = event.mouseMove.y;
    break;
  }
  case InputEventType::MouseScroll: {
    scrollX_ += event.mouseScroll.x;
    scrollY_ += event.mouseScroll.y;
    break;
  }
  case InputEventType::TextInput: {
    textInput_.push_back(event.textInput.codepoint);
    break;
  }
  }
}

bool InputSystem::IsKeyDown(Key key) const {
  return keys_[static_cast<size_t>(key)].down;
}

bool InputSystem::WasKeyPressed(Key key) const {
  return keys_[static_cast<size_t>(key)].pressed;
}

bool InputSystem::WasKeyReleased(Key key) const {
  return keys_[static_cast<size_t>(key)].released;
}

bool InputSystem::IsMouseDown(MouseButton button) const {
  return mouseButtons_[static_cast<size_t>(button)].down;
}

bool InputSystem::WasMousePressed(MouseButton button) const {
  return mouseButtons_[static_cast<size_t>(button)].pressed;
}

bool InputSystem::WasMouseReleased(MouseButton button) const {
  return mouseButtons_[static_cast<size_t>(button)].released;
}

float InputSystem::GetMouseX() const { return mouseX_; }
float InputSystem::GetMouseY() const { return mouseY_; }
float InputSystem::GetMouseDeltaX() const { return mouseDeltaX_; }
float InputSystem::GetMouseDeltaY() const { return mouseDeltaY_; }
float InputSystem::GetScrollX() const { return scrollX_; }
float InputSystem::GetScrollY() const { return scrollY_; }

const std::vector<uint32_t> &InputSystem::GetTextInput() const {
  return textInput_;
}

const std::vector<InputEvent> &InputSystem::GetEvents() const {
  return events_;
}

} // namespace Velos
