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

static Key TranslateKey(int key) {
  switch (key) {
  case GLFW_KEY_TAB:
    return Key::Tab;
  case GLFW_KEY_LEFT:
    return Key::LeftArrow;
  case GLFW_KEY_RIGHT:
    return Key::RightArrow;
  case GLFW_KEY_UP:
    return Key::UpArrow;
  case GLFW_KEY_DOWN:
    return Key::DownArrow;
  case GLFW_KEY_PAGE_UP:
    return Key::PageUp;
  case GLFW_KEY_PAGE_DOWN:
    return Key::PageDown;
  case GLFW_KEY_HOME:
    return Key::Home;
  case GLFW_KEY_END:
    return Key::End;
  case GLFW_KEY_INSERT:
    return Key::Insert;
  case GLFW_KEY_DELETE:
    return Key::DeleteKey;
  case GLFW_KEY_BACKSPACE:
    return Key::Backspace;
  case GLFW_KEY_SPACE:
    return Key::Space;
  case GLFW_KEY_ENTER:
    return Key::Enter;
  case GLFW_KEY_ESCAPE:
    return Key::Escape;
  case GLFW_KEY_A:
    return Key::A;
  case GLFW_KEY_B:
    return Key::B;
  // ...
  case GLFW_KEY_LEFT_SHIFT:
    return Key::LeftShift;
  case GLFW_KEY_LEFT_CONTROL:
    return Key::LeftCtrl;
  case GLFW_KEY_LEFT_ALT:
    return Key::LeftAlt;
  case GLFW_KEY_RIGHT_SHIFT:
    return Key::RightShift;
  case GLFW_KEY_RIGHT_CONTROL:
    return Key::RightCtrl;
  case GLFW_KEY_RIGHT_ALT:
    return Key::RightAlt;
  default:
    return Key::Unknown;
  }
}

static MouseButton TranslateMouseButton(int button) {
  switch (button) {
  case GLFW_MOUSE_BUTTON_LEFT:
    return MouseButton::Left;
  case GLFW_MOUSE_BUTTON_RIGHT:
    return MouseButton::Right;
  case GLFW_MOUSE_BUTTON_MIDDLE:
    return MouseButton::Middle;
  case GLFW_MOUSE_BUTTON_4:
    return MouseButton::Button4;
  case GLFW_MOUSE_BUTTON_5:
    return MouseButton::Button5;
  default:
    return MouseButton::Left;
  }
}

static void KeyCallback(GLFWwindow *window, int key, int scancode, int action,
                        int mods) {
  (void)scancode;
  (void)mods;

  auto *self = static_cast<GlfwWindow *>(glfwGetWindowUserPointer(window));
  if (!self || !self->input_)
    return;

  Key translated = TranslateKey(key);
  if (translated == Key::Unknown)
    return;

  if (action == GLFW_PRESS || action == GLFW_REPEAT) {
    InputEvent e{};
    e.type = InputEventType::KeyDown;
    e.key.key = translated;
    self->input_->ProcessEvent(e);
  } else if (action == GLFW_RELEASE) {
    InputEvent e{};
    e.type = InputEventType::KeyUp;
    e.key.key = translated;
    self->input_->ProcessEvent(e);
  }
}

static void CharCallback(GLFWwindow *window, unsigned int codepoint) {
  auto *self = static_cast<GlfwWindow *>(glfwGetWindowUserPointer(window));
  if (!self || !self->input_)
    return;

  InputEvent e{};
  e.type = InputEventType::TextInput;
  e.textInput.codepoint = codepoint;
  self->input_->ProcessEvent(e);
}

static void CursorPosCallback(GLFWwindow *window, double x, double y) {
  auto *self = static_cast<GlfwWindow *>(glfwGetWindowUserPointer(window));
  if (!self || !self->input_)
    return;

  InputEvent e{};
  e.type = InputEventType::MouseMove;
  e.mouseMove.x = static_cast<float>(x);
  e.mouseMove.y = static_cast<float>(y);
  self->input_->ProcessEvent(e);
}

static void MouseButtonCallback(GLFWwindow *window, int button, int action,
                                int mods) {
  (void)mods;

  auto *self = static_cast<GlfwWindow *>(glfwGetWindowUserPointer(window));
  if (!self || !self->input_)
    return;

  InputEvent e{};
  e.mouseButton.button = TranslateMouseButton(button);

  if (action == GLFW_PRESS) {
    e.type = InputEventType::MouseButtonDown;
    self->input_->ProcessEvent(e);
  } else if (action == GLFW_RELEASE) {
    e.type = InputEventType::MouseButtonUp;
    self->input_->ProcessEvent(e);
  }
}

static void ScrollCallback(GLFWwindow *window, double xoffset, double yoffset) {
  auto *self = static_cast<GlfwWindow *>(glfwGetWindowUserPointer(window));
  if (!self || !self->input_)
    return;

  InputEvent e{};
  e.type = InputEventType::MouseScroll;
  e.mouseScroll.x = static_cast<float>(xoffset);
  e.mouseScroll.y = static_cast<float>(yoffset);
  self->input_->ProcessEvent(e);
}

GlfwWindow::GlfwWindow(int width, int height, const std::string &title,
                       bool resizable, InputSystem *input)
    : windowWidth_(width), windowHeight_(height), title_(title), input_(input) {
  EnsureGlfwInitialized();

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);

  window_ = glfwCreateWindow(windowWidth_, windowHeight_, title_.c_str(),
                             nullptr, nullptr);
  if (!window_) {
    throw std::runtime_error("Failed to create GLFW window");
  }

  glfwSetWindowUserPointer(window_, this);

  glfwSetKeyCallback(window_, KeyCallback);
  glfwSetCharCallback(window_, CharCallback);
  glfwSetCursorPosCallback(window_, CursorPosCallback);
  glfwSetMouseButtonCallback(window_, MouseButtonCallback);
  glfwSetScrollCallback(window_, ScrollCallback);
}

GlfwWindow::~GlfwWindow() {
  if (window_) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
}

void GlfwWindow::PollEvents() {
  glfwPollEvents();

  glfwGetWindowSize(window_, &windowWidth_, &windowHeight_);
  glfwGetFramebufferSize(window_, &framebufferWidth_, &framebufferHeight_);
}

bool GlfwWindow::ShouldClose() const { return glfwWindowShouldClose(window_); }

int GlfwWindow::GetWidth() const { return windowWidth_; }

int GlfwWindow::GetHeight() const { return windowHeight_; }

int GlfwWindow::GetFramebufferWidth() const { return framebufferWidth_; }

int GlfwWindow::GetFramebufferHeight() const { return framebufferHeight_; }

const std::string &GlfwWindow::GetTitle() const { return title_; }

void *GlfwWindow::GetNativeHandle() const { return window_; }

} // namespace Velos
