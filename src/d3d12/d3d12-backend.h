#pragma once

#include "d3d12-base.h"
#include "../backend.h"

namespace rhi::d3d12 {

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

} // namespace rhi::d3d12
