#pragma once

#include <string>

namespace Velos {

struct NativeWindowHandle {
  void *handle = nullptr;
};

class Window {
public:
  virtual ~Window() = default;

  virtual void PollEvents() = 0;
  virtual bool ShouldClose() const = 0;

  virtual int GetWidth() const = 0;
  virtual int GetHeight() const = 0;
  virtual const std::string &GetTitle() const = 0;

  virtual void *GetNativeHandle() const = 0;
};

} // namespace Velos
