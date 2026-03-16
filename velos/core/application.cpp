#include "core/application.h"
#include "application.h"
#include "platform/glfw/glfw_window.h"

namespace Velos {

Application::Application(const ApplicationDesc &desc) {
  window_ = std::make_unique<GlfwWindow>(desc.width, desc.height, desc.title,
                                         desc.resizable);
}

Application::~Application() = default;

void Application::PollEvents() {
  window_->PollEvents();
  running_ = !window_->ShouldClose();
}

bool Application::IsRunning() const { return running_; }

Window &Application::GetWindow() { return *window_; }

void Application::Tick() {}

void Application::Close() { running_ = false; }

const Window &Application::GetWindow() const { return *window_; }

} // namespace Velos
