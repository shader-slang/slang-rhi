#include "cuda-shader-program.h"
#include "cuda-shader-object-layout.h"
#include "cuda-device.h"
#include "cuda-cubin.h"

namespace rhi::cuda {

ShaderProgramImpl::ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc)
    : ShaderProgram(device, desc)
{
}

ShaderProgramImpl::~ShaderProgramImpl() {}

Result ShaderProgramImpl::compileEntryPoint(Device* device, CompiledEntryPoint& entryPoint, bool measureCompilerTime)
{
    SLANG_RETURN_ON_FAIL(ShaderProgram::compileEntryPoint(device, entryPoint, measureCompilerTime));
    auto cudaDevice = static_cast<DeviceImpl*>(device);
    if (cudaDevice->m_cubinCompiler && entryPoint.stage == SLANG_STAGE_COMPUTE)
        return cudaDevice->m_cubinCompiler->convert(this, entryPoint);
    return SLANG_OK;
}

Result ShaderProgramImpl::createShaderModule(const ShaderModuleDesc& desc, ComPtr<ISlangBlob> kernelCode)
{
    Module module;
    module.stage = desc.stage;
    module.entryPointName = desc.entryPointName;
    module.code.assign(static_cast<const char*>(kernelCode->getBufferPointer()), kernelCode->getBufferSize());
    if (getDevice<DeviceImpl>()->m_cubinCompiler && desc.stage == SLANG_STAGE_COMPUTE)
    {
        // The compute override above always returns this backend artifact when opted in.
        auto artifact = static_cast<CubinArtifact*>(kernelCode.get());
        module.ptx = artifact->ptx;
        module.cubinCacheKey = artifact->cacheKey;
        module.cubinCacheHit = artifact->cacheHit;
    }
    m_modules.push_back(module);
    return SLANG_OK;
}

ShaderObjectLayout* ShaderProgramImpl::getRootShaderObjectLayout()
{
    return m_rootObjectLayout;
}

} // namespace rhi::cuda
