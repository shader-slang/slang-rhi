#include "d3d12-shader-program.h"
#include "d3d12-device.h"
#include "d3d12-shader-object-layout.h"

namespace rhi::d3d12 {

ShaderProgramImpl::ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc)
    : ShaderProgram(device, desc)
{
}

ShaderProgramImpl::~ShaderProgramImpl()
{
#if SLANG_RHI_ENABLE_AFTERMATH
    DeviceImpl* device = getDevice<DeviceImpl>();
    if (device->m_aftermathCrashDumper)
    {
        for (const ShaderBinary& shader : m_shaders)
        {
            device->m_aftermathCrashDumper->unregisterShader(reinterpret_cast<uint64_t>(shader.code.data()));
        }
    }
#endif
}

Result ShaderProgramImpl::createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode)
{
    ShaderBinary shaderBin;
    shaderBin.stage = entryPointInfo->getStage();
    shaderBin.entryPointInfo = entryPointInfo;
    shaderBin.code.assign(
        reinterpret_cast<const uint8_t*>(kernelCode->getBufferPointer()),
        reinterpret_cast<const uint8_t*>(kernelCode->getBufferPointer()) + (size_t)kernelCode->getBufferSize()
    );

#if SLANG_RHI_ENABLE_AFTERMATH
    DeviceImpl* device = getDevice<DeviceImpl>();
    if (device->m_aftermathCrashDumper)
    {
        device->m_aftermathCrashDumper->registerShader(
            reinterpret_cast<uint64_t>(shaderBin.code.data()),
            DeviceType::D3D12,
            shaderBin.code.data(),
            shaderBin.code.size()
        );
    }
#endif

    m_shaders.push_back(_Move(shaderBin));
    return SLANG_OK;
}

ShaderObjectLayout* ShaderProgramImpl::getRootShaderObjectLayout()
{
    return m_rootObjectLayout;
}

} // namespace rhi::d3d12
