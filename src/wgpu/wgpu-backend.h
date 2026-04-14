#pragma once

#include "wgpu-base.h"
#include "../backend.h"

namespace rhi::wgpu {

class BackendImpl : public Backend
{
public:
    Result initialize();

    std::vector<Adapter>& getAdapters() { return m_adapters; }

    // Backend implementation

    IAdapter* getAdapter(uint32_t index) override;
    Result createDevice(const DeviceDesc& desc, IDevice** outDevice) override;

private:
    std::vector<Adapter> m_adapters;
};

} // namespace rhi::wgpu
