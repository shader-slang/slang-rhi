#pragma once

#include "d3d12-base.h"

#include <string>
#include <vector>

namespace rhi::d3d12 {

struct ShaderBinary
{
    SlangStage stage;
    slang::EntryPointReflection* entryPointInfo;
    std::string actualEntryPointNameInAPI;
    std::vector<uint8_t> code;
};

class ShaderProgramImpl : public ShaderProgram
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    std::vector<ShaderBinary> m_shaders;

    virtual Result createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode)
        override;

    virtual ShaderObjectLayout* getRootShaderObjectLayout() override;
};

} // namespace rhi::d3d12
