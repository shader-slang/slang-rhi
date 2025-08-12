#include "webgpu-shader-program.h"
#include "webgpu-shader-object-layout.h"
#include "webgpu-device.h"

namespace rhi::webgpu {

ShaderProgramImpl::ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc)
    : ShaderProgram(device, desc)
{
}

ShaderProgramImpl::~ShaderProgramImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    for (Module& module : m_modules)
    {
        device->m_ctx.api.webgpuShaderModuleRelease(module.module);
    }
}

Result ShaderProgramImpl::createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    auto existingError = device->getAndClearLastError();
    if (existingError != WebGPUErrorType_NoError)
        device->printWarning("Web GPU device had reported error before shader compilation.");

    Module module;
    module.stage = entryPointInfo->getStage();
    module.entryPointName = entryPointInfo->getNameOverride();
    module.code = std::string((char*)kernelCode->getBufferPointer(), kernelCode->getBufferSize());

    WebGPUShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.chain.sType = WebGPUSType_ShaderSourceWGSL;
    wgslDesc.code.data = module.code.c_str();
    wgslDesc.code.length = module.code.size();
    WebGPUShaderModuleDescriptor desc = {};
    desc.nextInChain = (WebGPUChainedStruct*)&wgslDesc;

    module.module = device->m_ctx.api.webgpuDeviceCreateShaderModule(device->m_ctx.device, &desc);
    if (!module.module)
    {
        return SLANG_FAIL;
    }

    auto lastError = device->getAndClearLastError();
    if (lastError != WebGPUErrorType_NoError)
    {
        return SLANG_FAIL;
    }

    m_modules.push_back(module);
    return SLANG_OK;
}

ShaderObjectLayout* ShaderProgramImpl::getRootShaderObjectLayout()
{
    return m_rootObjectLayout;
}

ShaderProgramImpl::Module* ShaderProgramImpl::findModule(SlangStage stage)
{
    for (Module& module : m_modules)
    {
        if (module.stage == stage)
            return &module;
    }
    return nullptr;
}

Result DeviceImpl::createShaderProgram(
    const ShaderProgramDesc& desc,
    IShaderProgram** outProgram,
    ISlangBlob** outDiagnosticBlob
)
{
    RefPtr<ShaderProgramImpl> shaderProgram = new ShaderProgramImpl(this, desc);
    SLANG_RETURN_ON_FAIL(shaderProgram->init());
    SLANG_RETURN_ON_FAIL(
        RootShaderObjectLayoutImpl::create(
            this,
            shaderProgram->linkedProgram,
            shaderProgram->linkedProgram->getLayout(),
            shaderProgram->m_rootObjectLayout.writeRef()
        )
    );
    returnComPtr(outProgram, shaderProgram);
    return SLANG_OK;
}

} // namespace rhi::webgpu
