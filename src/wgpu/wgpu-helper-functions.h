#pragma once

#include <slang-rhi.h>

#include <vector>

namespace rhi {

Result SLANG_MCALL getWGPUAdapters(std::vector<AdapterInfo>& outAdapters);
Result SLANG_MCALL createWGPUDevice(const IDevice::Desc* desc, IDevice** outRenderer);

} // namespace rhi
