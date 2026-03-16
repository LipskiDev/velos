#pragma once

#include <memory>
#include <string>

namespace Velos {

struct ApplicationDesc {
  int width = 1280;
  int height = 720;
  std::string title = "Velos";
  bool resizable = true;
};

class Window;

class Application {
public:
  explicit Application(const ApplicationDesc &desc);
  ~Application();

  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;

  void PollEvents();
  bool IsRunning() const;
  void Tick();

  void Close();

  Window &GetWindow();
  const Window &GetWindow() const;

private:
  std::unique_ptr<Window> window_;
  bool running_ = true;
};

} // namespace Velos
