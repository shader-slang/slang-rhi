#pragma once

#include "vk-base.h"
#include "../backend.h"

namespace rhi::vk {

class BackendImpl : public Backend
{
public:
    std::span<const AdapterImpl> getAdapters();

    // Backend implementation

    IAdapter* getAdapter(uint32_t index) override;
    Result createDevice(const DeviceDesc& desc, IDevice** outDevice) override;

protected:
    Result enumerateAdapters() override;

private:
    std::vector<AdapterImpl> m_adapters;
};

} // namespace rhi::vk
