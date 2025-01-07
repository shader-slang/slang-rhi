#pragma once

#include "cuda-base.h"
#include "cuda-shader-object-layout.h"

namespace rhi::cuda {

class ShaderProgramImpl : public ShaderProgram
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;

    struct Module
    {
        SlangStage stage;
        std::string entryPointName;
        ComPtr<ISlangBlob> code;
    };

    std::vector<Module> m_modules;

    ~ShaderProgramImpl();

    virtual Result createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode)
        override;
};

} // namespace rhi::cuda
