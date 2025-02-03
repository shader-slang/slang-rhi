#pragma once

#include "metal-base.h"

#include <vector>

namespace rhi {

Result SLANG_MCALL getMetalAdapters(std::vector<AdapterInfo>& outAdapters);
Result SLANG_MCALL createMetalDevice(const DeviceDesc* desc, IDevice** outRenderer);

} // namespace rhi
