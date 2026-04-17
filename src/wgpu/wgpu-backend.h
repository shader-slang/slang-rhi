#pragma once

#include "wgpu-base.h"
#include "../backend.h"

namespace rhi::wgpu {

class BackendImpl : public Backend
{
public:
    std::span<const Adapter> getAdapters();

    // Backend implementation

    IAdapter* getAdapter(uint32_t index) override;
    Result createDevice(const DeviceDesc& desc, IDevice** outDevice) override;

protected:
    Result enumerateAdapters() override;

private:
    std::vector<Adapter> m_adapters;
};

} // namespace rhi::wgpu
