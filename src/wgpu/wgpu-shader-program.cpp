#include "wgpu-shader-program.h"
#include "wgpu-shader-object-layout.h"
#include "wgpu-device.h"

namespace rhi::wgpu {

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
    module.code = std::string((char*)kernelCode->getBufferPointer(), kernelCode->getBufferSize());

    WGPUShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code.data = module.code.c_str();
    wgslDesc.code.length = module.code.size();
    WGPUShaderModuleDescriptor desc = {};
    desc.nextInChain = (WGPUChainedStruct*)&wgslDesc;

    module.module = m_device->m_ctx.api.wgpuDeviceCreateShaderModule(m_device->m_ctx.device, &desc);
    if (!module.module)
    {
        return SLANG_FAIL;
    }

    m_modules.push_back(module);
    return SLANG_OK;
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
    RefPtr<ShaderProgramImpl> shaderProgram = new ShaderProgramImpl(this);
    shaderProgram->init(desc);

    RootShaderObjectLayout::create(
        this,
        shaderProgram->linkedProgram,
        shaderProgram->linkedProgram->getLayout(),
        shaderProgram->m_rootObjectLayout.writeRef()
    );

    if (!shaderProgram->isSpecializable())
    {
        SLANG_RETURN_ON_FAIL(shaderProgram->compileShaders(this));
    }

    returnComPtr(outProgram, shaderProgram);
    return SLANG_OK;
}

} // namespace rhi::wgpu
