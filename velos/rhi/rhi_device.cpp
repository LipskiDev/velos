#include "rhi_device.h"
#include "vulkan/vk_device.h"

#include <stdexcept>

namespace Velos::RHI {

IDevice *CreateDevice(const DeviceDesc &desc) {
  switch (desc.backend) {
  case BackendAPI::Vulkan:
    return new VulkanDevice(desc);

  default:
    throw std::runtime_error("Unsupported RHI backend");
  }
}

void DestroyDevice(IDevice *device) { delete device; }

} // namespace Velos::RHI
