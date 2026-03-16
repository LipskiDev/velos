#pragma once

#include "core/window.h"
#include <string>

struct GLFWwindow;

namespace Velos {

class GlfwWindow final : public Window {
public:
  GlfwWindow(int width, int height, const std::string &title, bool resizable);
  ~GlfwWindow() override;

  void PollEvents() override;
  bool ShouldClose() const override;

  int GetWidth() const override;
  int GetHeight() const override;
  const std::string &GetTitle() const override;

  void *GetNativeHandle() const override;

private:
  GLFWwindow *window_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  std::string title_;
};

} // namespace Velos
