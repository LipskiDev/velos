#include "device.h"
#include "vulkan/device.h"

#include <stdexcept>

namespace Velos::RHI {

IDevice *CreateDevice(const DeviceDesc &desc) {
  switch (desc.graphicsAPI) {
  case GraphicsAPI::Vulkan:
    return new Vulkan::Device(desc);

  default:
    throw std::runtime_error("Unsupported RHI backend");
  }
}

void DestroyDevice(IDevice *device) { delete device; }

} // namespace Velos::RHI
