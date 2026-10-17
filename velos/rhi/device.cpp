#include "device.h"
#include "vulkan/device.h"
#if defined(VL_HAS_D3D12)
#include "d3d12/device.h"
#endif

#include <stdexcept>

namespace Velos::RHI {

IDevice *CreateDevice(const DeviceDesc &desc) {
  switch (desc.graphicsAPI) {
    case GraphicsAPI::Vulkan:
      return new Vulkan::Device(desc);
#if defined(VL_HAS_D3D12)
    case GraphicsAPI::D3D12:
      return new D3D12::Device(desc);
#endif
    default:
      throw std::runtime_error("Unsupported RHI backend");
  }
}

void DestroyDevice(IDevice *device) {
  delete device;
}

}  // namespace Velos::RHI
