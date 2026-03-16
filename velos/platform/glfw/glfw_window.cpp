#include "platform/glfw/glfw_window.h"

#include <GLFW/glfw3.h>
#include <stdexcept>

#include <iostream>

namespace Velos {

namespace {
inline void EnsureGlfwInitialized() {
  static bool initialized = false;

  if (!initialized) {
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    if (!glfwInit()) {
      throw std::runtime_error("Failed to initialize GLFW");
    }
    initialized = true;
  }
}
} // namespace

GlfwWindow::GlfwWindow(int width, int height, const std::string &title,
                       bool resizable)
    : width_(width), height_(height), title_(title) {
  EnsureGlfwInitialized();

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);

  window_ = glfwCreateWindow(width_, height_, title_.c_str(), nullptr, nullptr);
  if (!window_) {
    throw std::runtime_error("Failed to create GLFW window");
  }

  // Temporary GL context just to make Wayland show the window
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
  GLFWwindow *tmp = glfwCreateWindow(1, 1, "", nullptr, nullptr);
  glfwMakeContextCurrent(tmp);
  glfwSwapBuffers(tmp);
  glfwDestroyWindow(tmp);
}

GlfwWindow::~GlfwWindow() {
  if (window_) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
}

void GlfwWindow::PollEvents() {
  glfwPollEvents();

  int w = 0;
  int h = 0;
  glfwGetFramebufferSize(window_, &w, &h);
  width_ = w;
  height_ = h;
}

bool GlfwWindow::ShouldClose() const { return glfwWindowShouldClose(window_); }

int GlfwWindow::GetWidth() const { return width_; }

int GlfwWindow::GetHeight() const { return height_; }

const std::string &GlfwWindow::GetTitle() const { return title_; }

void *GlfwWindow::GetNativeHandle() const { return window_; }

} // namespace Velos
