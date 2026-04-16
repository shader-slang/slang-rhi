#include "wgpu-backend.h"
#include "wgpu-device.h"
#include "wgpu-utils.h"
#include "../reference.h"

#include "core/deferred.h"
#include "core/string.h"

namespace rhi::wgpu {

std::span<const Adapter> BackendImpl::getAdapters()
{
    ensureAdapters();
    return m_adapters;
}

IAdapter* BackendImpl::getAdapter(uint32_t index)
{
    ensureAdapters();
    return index < m_adapters.size() ? &m_adapters[index] : nullptr;
}

Result BackendImpl::createDevice(const DeviceDesc& desc, IDevice** outDevice)
{
    RefPtr<DeviceImpl> result = new DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(desc, this));
    returnComPtr(outDevice, result);
    return SLANG_OK;
}

Result BackendImpl::enumerateAdapters()
{
    // If WGPU is not available, return no adapters.
    API api;
    if (SLANG_FAILED(api.init()))
    {
        return SLANG_OK;
    }

    WGPUInstance wgpuInstance = {};
    SLANG_RETURN_ON_FAIL(createWGPUInstance(api, &wgpuInstance));
    SLANG_RHI_DEFERRED({ api.wgpuInstanceRelease(wgpuInstance); });

    WGPUAdapter wgpuAdapter = {};
    SLANG_RETURN_ON_FAIL(createWGPUAdapter(api, wgpuInstance, &wgpuAdapter));
    SLANG_RHI_DEFERRED({ api.wgpuAdapterRelease(wgpuAdapter); });

    WGPUAdapterInfo wgpuAdapterInfo = {};
    api.wgpuAdapterGetInfo(wgpuAdapter, &wgpuAdapterInfo);

    AdapterInfo info = {};
    info.deviceType = DeviceType::WGPU;
    switch (wgpuAdapterInfo.adapterType)
    {
    case WGPUAdapterType_DiscreteGPU:
        info.adapterType = AdapterType::Discrete;
        break;
    case WGPUAdapterType_IntegratedGPU:
        info.adapterType = AdapterType::Integrated;
        break;
    case WGPUAdapterType_CPU:
        info.adapterType = AdapterType::Software;
        break;
    default:
        info.adapterType = AdapterType::Unknown;
        break;
    }
    string::copy_safe(info.name, sizeof(info.name), wgpuAdapterInfo.device.data, wgpuAdapterInfo.device.length);
    info.vendorID = wgpuAdapterInfo.vendorID;
    info.deviceID = wgpuAdapterInfo.deviceID;

    Adapter adapter;
    adapter.m_info = info;
    adapter.m_isDefault = true;
    m_adapters.push_back(adapter);

    return SLANG_OK;
}

} // namespace rhi::wgpu

namespace rhi {

Result createWGPUBackend(Backend** outBackend)
{
    RefPtr<wgpu::BackendImpl> backend = new wgpu::BackendImpl();
    returnRefPtr(outBackend, backend);
    return SLANG_OK;
}

} // namespace rhi
