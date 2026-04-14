#pragma once

#include <slang-rhi.h>

#include "core/smart-pointer.h"

namespace rhi {

class Backend : public RefObject
{
public:
    virtual ~Backend() = default;
    virtual IAdapter* getAdapter(uint32_t index) = 0;
    virtual Result createDevice(const DeviceDesc& desc, IDevice** outDevice) = 0;
};

Result createD3D11Backend(Backend** outBackend);
Result createD3D12Backend(Backend** outBackend);
Result createVKBackend(Backend** outBackend);
Result createMetalBackend(Backend** outBackend);
Result createCUDABackend(Backend** outBackend);
Result createCPUBackend(Backend** outBackend);
Result createWGPUBackend(Backend** outBackend);

} // namespace rhi
