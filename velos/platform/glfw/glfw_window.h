#pragma once

#include "core/input_system.h"
#include "core/window.h"
#include <string>

struct GLFWwindow;

namespace Velos {

class GlfwWindow final : public Window {
public:
  GlfwWindow(int width, int height, const std::string &title, bool resizable,
             InputSystem *input);
  ~GlfwWindow() override;

  void PollEvents() override;
  bool ShouldClose() const override;

  int GetWidth() const override;
  int GetHeight() const override;

  int GetFramebufferWidth() const override;
  int GetFramebufferHeight() const override;

  bool WasFramebufferResized() const override { return framebufferResized_; }

  void ResetFramebufferResizedFlag() override { framebufferResized_ = false; }

  const std::string &GetTitle() const override;

  void *GetNativeHandle() const override;

public:
  InputSystem *input_ = nullptr;

  int windowWidth_ = 0;
  int windowHeight_ = 0;
  int framebufferWidth_ = 0;
  int framebufferHeight_ = 0;

  bool framebufferResized_ = false;

private:
  GLFWwindow *window_ = nullptr;

  std::string title_;
};

} // namespace Velos
