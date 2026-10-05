#pragma once

#include "d3d12-base.h"

#include <string>
#include <vector>
#include <memory>

namespace rhi::d3d12 {

struct ShaderBinary
{
    SlangStage stage;
    std::string entryPointName;
    std::vector<uint8_t> code;

    // Used only by the ARM64 diagnostic workflow. When enabled, this owns a
    // page-backed copy whose requested trailing bytes end at a guard page.
    std::shared_ptr<void> diagnosticAllocation;
    const uint8_t* diagnosticCode = nullptr;

    const uint8_t* data() const { return diagnosticCode ? diagnosticCode : code.data(); }
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
