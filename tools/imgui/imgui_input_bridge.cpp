#include "imgui_input_bridge.h"

#include "core/input_system.h"
#include "core/input_types.h"

#include <imgui.h>
using namespace Velos;

namespace {

static ImGuiKey ToImGuiKey(Key key) {
  switch (key) {
  case Key::Tab:
    return ImGuiKey_Tab;
  case Key::LeftArrow:
    return ImGuiKey_LeftArrow;
  case Key::RightArrow:
    return ImGuiKey_RightArrow;
  case Key::UpArrow:
    return ImGuiKey_UpArrow;
  case Key::DownArrow:
    return ImGuiKey_DownArrow;
  case Key::PageUp:
    return ImGuiKey_PageUp;
  case Key::PageDown:
    return ImGuiKey_PageDown;
  case Key::Home:
    return ImGuiKey_Home;
  case Key::End:
    return ImGuiKey_End;
  case Key::Insert:
    return ImGuiKey_Insert;
  case Key::DeleteKey:
    return ImGuiKey_Delete;
  case Key::Backspace:
    return ImGuiKey_Backspace;
  case Key::Space:
    return ImGuiKey_Space;
  case Key::Enter:
    return ImGuiKey_Enter;
  case Key::Escape:
    return ImGuiKey_Escape;
  case Key::A:
    return ImGuiKey_A;
  case Key::B:
    return ImGuiKey_B;
  case Key::C:
    return ImGuiKey_C;
  // ...
  case Key::LeftCtrl:
    return ImGuiKey_LeftCtrl;
  case Key::RightCtrl:
    return ImGuiKey_RightCtrl;
  case Key::LeftShift:
    return ImGuiKey_LeftShift;
  case Key::RightShift:
    return ImGuiKey_RightShift;
  case Key::LeftAlt:
    return ImGuiKey_LeftAlt;
  case Key::RightAlt:
    return ImGuiKey_RightAlt;
  default:
    return ImGuiKey_None;
  }
}

static int ToImGuiMouseButton(MouseButton button) {
  switch (button) {
  case MouseButton::Left:
    return 0;
  case MouseButton::Right:
    return 1;
  case MouseButton::Middle:
    return 2;
  case MouseButton::Button4:
    return 3;
  case MouseButton::Button5:
    return 4;
  default:
    return 0;
  }
}

} // namespace

void ImGuiInputBridge::Apply(const InputSystem &input) {
  ImGuiIO &io = ImGui::GetIO();

  for (const InputEvent &e : input.GetEvents()) {
    switch (e.type) {
    case InputEventType::KeyDown: {
      ImGuiKey key = ToImGuiKey(e.key.key);
      if (key != ImGuiKey_None)
        io.AddKeyEvent(key, true);
      break;
    }
    case InputEventType::KeyUp: {
      ImGuiKey key = ToImGuiKey(e.key.key);
      if (key != ImGuiKey_None)
        io.AddKeyEvent(key, false);
      break;
    }
    case InputEventType::MouseButtonDown:
      io.AddMouseButtonEvent(ToImGuiMouseButton(e.mouseButton.button), true);
      break;
    case InputEventType::MouseButtonUp:
      io.AddMouseButtonEvent(ToImGuiMouseButton(e.mouseButton.button), false);
      break;
    case InputEventType::MouseMove:
      io.AddMousePosEvent(e.mouseMove.x, e.mouseMove.y);
      break;
    case InputEventType::MouseScroll:
      io.AddMouseWheelEvent(e.mouseScroll.x, e.mouseScroll.y);
      break;
    case InputEventType::TextInput:
      io.AddInputCharacter(e.textInput.codepoint);
      break;
    }
  }
}
