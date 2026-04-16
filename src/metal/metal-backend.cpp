#include "metal-backend.h"
#include "metal-device.h"
#include "metal-utils.h"
#include "../reference.h"

#include "core/string.h"

namespace rhi::metal {

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
    AUTORELEASEPOOL

    auto addAdapter = [&](MTL::Device* device)
    {
        AdapterInfo info = {};
        info.deviceType = DeviceType::Metal;
        info.adapterType = device->hasUnifiedMemory() ? AdapterType::Integrated : AdapterType::Discrete;
        const char* name = device->name()->cString(NS::ASCIIStringEncoding);
        string::copy_safe(info.name, sizeof(info.name), name);
        uint64_t registryID = device->registryID();
        memcpy(&info.luid.luid[0], &registryID, sizeof(registryID));

        AdapterImpl adapter;
        adapter.m_info = info;
        adapter.m_device = NS::RetainPtr(device);
        m_adapters.push_back(adapter);
    };

    NS::Array* devices = MTL::CopyAllDevices();
    if (devices->count() > 0)
    {
        for (int i = 0; i < devices->count(); ++i)
        {
            MTL::Device* device = static_cast<MTL::Device*>(devices->object(i));
            addAdapter(device);
        }
    }
    else
    {
        MTL::Device* device = MTL::CreateSystemDefaultDevice();
        addAdapter(device);
        device->release();
    }

    // Make the first adapter the default one.
    if (!m_adapters.empty())
    {
        m_adapters[0].m_isDefault = true;
    }

    return SLANG_OK;
}

} // namespace rhi::metal

namespace rhi {

Result createMetalBackend(Backend** outBackend)
{
    RefPtr<metal::BackendImpl> backend = new metal::BackendImpl();
    returnRefPtr(outBackend, backend);
    return SLANG_OK;
}

} // namespace rhi
