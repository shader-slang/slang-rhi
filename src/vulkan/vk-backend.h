#pragma once

#include "vk-base.h"
#include "../backend.h"

namespace rhi::vk {

class BackendImpl : public Backend
{
public:
    ~BackendImpl() override;

    std::span<const AdapterImpl> getAdapters();

    // Backend implementation

    IAdapter* getAdapter(uint32_t index) override;
    Result createDevice(const DeviceDesc& desc, IDevice** outDevice) override;

protected:
    Result enumerateAdapters() override;

private:
    void releaseAdapterEnumerationContext();

    std::vector<AdapterImpl> m_adapters;
    VulkanModule m_adapterEnumerationModule;
    VulkanApi m_adapterEnumerationApi;
    VkInstance m_adapterEnumerationInstance = VK_NULL_HANDLE;
};

} // namespace rhi::vk
