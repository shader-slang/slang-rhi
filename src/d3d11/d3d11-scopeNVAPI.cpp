#include "d3d11-scopeNVAPI.h"
#include "d3d11-device.h"

namespace rhi::d3d11 {

Result ScopeNVAPI::init(DeviceImpl* device, Index regIndex)
{
    if (!device->m_nvapi)
    {
        // There is nothing to set as nvapi is not set
        return SLANG_OK;
    }

#if SLANG_RHI_ENABLE_NVAPI
    NvAPI_Status nvapiStatus = NvAPI_D3D11_SetNvShaderExtnSlot(device->m_device, NvU32(regIndex));
    if (nvapiStatus != NVAPI_OK)
    {
        return SLANG_FAIL;
    }
#endif

    // Record the device so it can be freed
    m_device = device;
    return SLANG_OK;
}

ScopeNVAPI::~ScopeNVAPI()
{
    // If the m_device is not set, it must not have been set up
    if (m_device)
    {
#if SLANG_RHI_ENABLE_NVAPI
        // Disable the slot used
        NvAPI_Status nvapiStatus = NvAPI_D3D11_SetNvShaderExtnSlot(m_device->m_device, ~0);
        SLANG_RHI_ASSERT(nvapiStatus == NVAPI_OK);
#endif
    }
}

} // namespace rhi::d3d11
