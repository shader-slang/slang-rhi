#include "d3d12-backend.h"
#include "d3d12-device.h"
#include "../d3d/d3d-utils.h"
#include "../reference.h"

#include "core/string.h"

namespace rhi::d3d12 {

std::span<const AdapterImpl> BackendImpl::getAdapters()
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
    std::vector<ComPtr<IDXGIAdapter>> dxgiAdapters;
    SLANG_RETURN_ON_FAIL(enumAdapters(dxgiAdapters));

    for (const auto& dxgiAdapter : dxgiAdapters)
    {
        AdapterInfo info = getAdapterInfo(dxgiAdapter);
        info.deviceType = DeviceType::D3D12;

        AdapterImpl adapter;
        adapter.m_info = info;
        adapter.m_dxgiAdapter = dxgiAdapter;
        m_adapters.push_back(adapter);
    }

    // Mark default adapter (prefer discrete if available).
    markDefaultAdapter(m_adapters);

    return SLANG_OK;
}

} // namespace rhi::d3d12

namespace rhi {

Result createD3D12Backend(Backend** outBackend)
{
    RefPtr<d3d12::BackendImpl> backend = new d3d12::BackendImpl();
    returnRefPtr(outBackend, backend);
    return SLANG_OK;
}

} // namespace rhi
