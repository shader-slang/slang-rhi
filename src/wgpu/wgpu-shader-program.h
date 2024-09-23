#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class ShaderProgramImpl : public ShaderProgram
{
public:
    BreakableReference<DeviceImpl> m_device;
    RefPtr<RootShaderObjectLayout> m_rootObjectLayout;

    struct Module
    {
        SlangStage stage;
        std::string entryPointName;
        std::string code;
        WGPUShaderModule module = nullptr;
    };

    std::vector<Module> m_modules;

    ShaderProgramImpl(DeviceImpl* device);
    ~ShaderProgramImpl();

    virtual Result createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode)
        override;
};

} // namespace rhi::wgpu
