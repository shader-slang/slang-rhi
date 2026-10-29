#include "metal-shader-program.h"
#include "metal-device.h"
#include "metal-shader-object-layout.h"
#include "metal-utils.h"

namespace rhi::metal {

ShaderProgramImpl::ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc)
    : ShaderProgram(device, desc)
{
}

ShaderProgramImpl::~ShaderProgramImpl() {}

Result ShaderProgramImpl::createShaderModule(const ShaderModuleDesc& desc, ComPtr<ISlangBlob> kernelCode)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    Module module;
    module.stage = desc.stage;
    module.entryPointName = desc.entryPointName;
    module.code = kernelCode;

    dispatch_data_t data = dispatch_data_create(
        kernelCode->getBufferPointer(),
        kernelCode->getBufferSize(),
        dispatch_get_main_queue(),
        NULL
    );
    NS::Error* error;
    module.library = NS::TransferPtr(device->m_device->newLibrary(data, &error));
    if (!module.library)
    {
        const char* msg = error->localizedDescription()->utf8String();
        device->handleMessage(DebugMessageType::Error, DebugMessageSource::Driver, msg);
        return SLANG_E_INVALID_ARG;
    }

    m_modules.push_back(module);
    return SLANG_OK;
}

ShaderObjectLayout* ShaderProgramImpl::getRootShaderObjectLayout()
{
    return m_rootObjectLayout;
}

} // namespace rhi::metal
