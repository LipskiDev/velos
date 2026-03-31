#pragma once

namespace Velos {
class InputSystem;
}

class ImGuiInputBridge {
public:
  static void Apply(const Velos::InputSystem &input);
};
