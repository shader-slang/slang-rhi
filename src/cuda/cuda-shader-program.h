#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

class ShaderProgramImpl : public ShaderProgram
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;

    struct Module
    {
        SlangStage stage;
        std::string entryPointName;
        // Length-delimited PTX or CUBIN. c_str() supplies the terminator required for PTX.
        std::string code;
        ComPtr<ISlangBlob> ptx;
        ComPtr<ISlangBlob> cubinCacheKey;
        bool cubinCacheHit = false;
    };

    std::vector<Module> m_modules;

    ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc);
    ~ShaderProgramImpl();

    Result compileEntryPoint(Device* device, CompiledEntryPoint& entryPoint, bool measureCompilerTime) override;

    virtual Result createShaderModule(const ShaderModuleDesc& desc, ComPtr<ISlangBlob> kernelCode) override;

    virtual ShaderObjectLayout* getRootShaderObjectLayout() override;
};

} // namespace rhi::cuda
