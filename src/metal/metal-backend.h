#pragma once

#include "metal-base.h"
#include "../backend.h"

namespace rhi::metal {

class BackendImpl : public Backend
{
public:
    Result initialize();

    std::vector<AdapterImpl>& getAdapters() { return m_adapters; }

    // Backend implementation

    IAdapter* getAdapter(uint32_t index) override;
    Result createDevice(const DeviceDesc& desc, IDevice** outDevice) override;

private:
    std::vector<AdapterImpl> m_adapters;
};

} // namespace rhi::metal
