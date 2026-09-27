#pragma once

#include "d3d12-base.h"

#include <string>
#include <vector>

namespace rhi::d3d12 {

struct ShaderBinary
{
    SlangStage stage;
    std::string entryPointName;
    std::vector<uint8_t> code;
};

class ShaderProgramImpl : public ShaderProgram
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    std::vector<ShaderBinary> m_shaders;

    ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc);
    ~ShaderProgramImpl();

    virtual Result createShaderModule(const ShaderModuleDesc& desc, ComPtr<ISlangBlob> kernelCode) override;

    virtual ShaderObjectLayout* getRootShaderObjectLayout() override;
};

} // namespace rhi::d3d12
