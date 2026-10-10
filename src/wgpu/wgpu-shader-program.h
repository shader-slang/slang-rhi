#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class ShaderProgramImpl : public ShaderProgram
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;

    struct UsedBinding
    {
        uint32_t group;
        uint32_t binding;
    };

    struct Module
    {
        SlangStage stage;
        std::string entryPointName;
        std::string code;
        std::vector<UsedBinding> usedBindings;
        WGPUShaderModule module = nullptr;
    };

    std::vector<Module> m_modules;

    ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc);
    ~ShaderProgramImpl();

    virtual Result compileEntryPoint(Device* device, CompiledEntryPoint& entryPoint, bool measureCompilerTime) override;

    virtual Result createShaderModule(const ShaderModuleDesc& desc, ComPtr<ISlangBlob> kernelCode) override;

    virtual ShaderObjectLayout* getRootShaderObjectLayout() override;

    Module* findModule(SlangStage stage);
};

} // namespace rhi::wgpu
