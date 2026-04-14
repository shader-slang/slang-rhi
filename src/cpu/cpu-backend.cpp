#include "cpu-backend.h"
#include "cpu-device.h"
#include "../reference.h"

#include "core/string.h"

namespace rhi::cpu {

Result BackendImpl::initialize()
{
    AdapterInfo info = {};
    info.deviceType = DeviceType::CPU;
    info.adapterType = AdapterType::Software;
    string::copy_safe(info.name, sizeof(info.name), "Default");
    m_adapter.m_info = info;
    m_adapter.m_isDefault = true;
    return SLANG_OK;
}

IAdapter* BackendImpl::getAdapter(uint32_t index)
{
    return index == 0 ? &m_adapter : nullptr;
}

Result BackendImpl::createDevice(const DeviceDesc& desc, IDevice** outDevice)
{
    RefPtr<DeviceImpl> result = new DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(desc, this));
    returnComPtr(outDevice, result);
    return SLANG_OK;
}

} // namespace rhi::cpu

namespace rhi {

Result createCPUBackend(Backend** outBackend)
{
    RefPtr<cpu::BackendImpl> backend = new cpu::BackendImpl();
    SLANG_RETURN_ON_FAIL(backend->initialize());
    returnRefPtr(outBackend, backend);
    return SLANG_OK;
}

} // namespace rhi
