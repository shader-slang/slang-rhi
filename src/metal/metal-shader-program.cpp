#include "metal-shader-program.h"
#include "metal-device.h"
#include "metal-shader-object-layout.h"
#include "metal-util.h"

namespace rhi::metal {

ShaderProgramImpl::ShaderProgramImpl(DeviceImpl* device)
    : m_device(device)
{
}

ShaderProgramImpl::~ShaderProgramImpl() {}

Result ShaderProgramImpl::createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode)
{
    Module module;
    module.stage = entryPointInfo->getStage();
    module.entryPointName = entryPointInfo->getNameOverride();
    module.code = kernelCode;

    dispatch_data_t data = dispatch_data_create(
        kernelCode->getBufferPointer(),
        kernelCode->getBufferSize(),
        dispatch_get_main_queue(),
        NULL
    );
    NS::Error* error;
    module.library = NS::TransferPtr(m_device->m_device->newLibrary(data, &error));
    if (!module.library)
    {
        const char* msg = error->localizedDescription()->utf8String();
        m_device->handleMessage(DebugMessageType::Error, DebugMessageSource::Driver, msg);
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
