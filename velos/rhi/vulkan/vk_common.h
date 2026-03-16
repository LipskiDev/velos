#pragma once

#include <volk.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace Velos::RHI {
inline void VK_CHECK(VkResult result, const char *message) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(message);
  }
}
} // namespace Velos::RHI
