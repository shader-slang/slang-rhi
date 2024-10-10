#include "wgpu-helper-functions.h"
#include "wgpu-device.h"

namespace rhi {

Result SLANG_MCALL getWGPUAdapters(std::vector<AdapterInfo>& outAdapters)
{
#if 0
    auto addAdapter = [&](MTL::Device* device)
    {
        AdapterInfo info = {};
        const char* name = device->name()->cString(NS::ASCIIStringEncoding);
        memcpy(info.name, name, min(strlen(name), sizeof(AdapterInfo::name) - 1));
        uint64_t registryID = device->registryID();
        memcpy(&info.luid.luid[0], &registryID, sizeof(registryID));
        outAdapters.push_back(info);
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
#endif
    return SLANG_OK;
}

Result SLANG_MCALL createWGPUDevice(const DeviceDesc* desc, IDevice** outRenderer)
{
    RefPtr<wgpu::DeviceImpl> result = new wgpu::DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(*desc));
    returnComPtr(outRenderer, result);
    return SLANG_OK;
}

} // namespace rhi
