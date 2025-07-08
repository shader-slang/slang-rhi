#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class ShaderProgramImpl : public ShaderProgram
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;

    struct Module
    {
        SlangStage stage;
        ComPtr<ISlangBlob> code;
    };

    std::vector<Module> m_modules;

    ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc);

    virtual Result createShaderModule(
        slang::EntryPointReflection* entryPointInfo,
        ComPtr<ISlangBlob> kernelCode
    ) override;

    virtual ShaderObjectLayout* getRootShaderObjectLayout() override;

    Module* findModule(SlangStage stage);
};

} // namespace rhi::d3d11
