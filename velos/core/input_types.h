#pragma once

#include <cstdint>

namespace Velos {

enum class Key : uint16_t {
  Unknown = 0,

  Tab,
  LeftArrow,
  RightArrow,
  UpArrow,
  DownArrow,
  PageUp,
  PageDown,
  Home,
  End,
  Insert,
  DeleteKey,
  Backspace,
  Space,
  Enter,
  Escape,

  Apostrophe,
  Comma,
  Minus,
  Period,
  Slash,
  Semicolon,
  Equal,
  LeftBracket,
  Backslash,
  RightBracket,
  GraveAccent,

  CapsLock,
  ScrollLock,
  NumLock,
  PrintScreen,
  Pause,

  Keypad0,
  Keypad1,
  Keypad2,
  Keypad3,
  Keypad4,
  Keypad5,
  Keypad6,
  Keypad7,
  Keypad8,
  Keypad9,
  KeypadDecimal,
  KeypadDivide,
  KeypadMultiply,
  KeypadSubtract,
  KeypadAdd,
  KeypadEnter,
  KeypadEqual,

  LeftShift,
  LeftCtrl,
  LeftAlt,
  LeftSuper,
  RightShift,
  RightCtrl,
  RightAlt,
  RightSuper,
  Menu,

  Num0,
  Num1,
  Num2,
  Num3,
  Num4,
  Num5,
  Num6,
  Num7,
  Num8,
  Num9,

  A,
  B,
  C,
  D,
  E,
  F,
  G,
  H,
  I,
  J,
  K,
  L,
  M,
  N,
  O,
  P,
  Q,
  R,
  S,
  T,
  U,
  V,
  W,
  X,
  Y,
  Z,

  F1,
  F2,
  F3,
  F4,
  F5,
  F6,
  F7,
  F8,
  F9,
  F10,
  F11,
  F12,

  Count
};

enum class MouseButton : uint8_t {
  Left = 0,
  Right,
  Middle,
  Button4,
  Button5,
  Count
};

enum class InputEventType : uint8_t {
  KeyDown,
  KeyUp,
  MouseButtonDown,
  MouseButtonUp,
  MouseMove,
  MouseScroll,
  TextInput
};

struct InputEvent {
  InputEventType type = InputEventType::MouseMove;

  union {
    struct {
      Key key;
    } key;

    struct {
      MouseButton button;
    } mouseButton;

    struct {
      float x;
      float y;
    } mouseMove;

    struct {
      float x;
      float y;
    } mouseScroll;

    struct {
      uint32_t codepoint;
    } textInput;
  };
};

} // namespace Velos
