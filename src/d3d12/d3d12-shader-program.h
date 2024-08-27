// d3d12-shader-program.h
#pragma once

#include "d3d12-base.h"
#include "d3d12-shader-object-layout.h"

#include <vector>
#include <string>

namespace rhi
{
namespace d3d12
{

using namespace Slang;

struct ShaderBinary
{
    SlangStage stage;
    slang::EntryPointReflection* entryPointInfo;
    std::string actualEntryPointNameInAPI;
    std::vector<uint8_t> code;
};

class ShaderProgramImpl : public ShaderProgramBase
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    std::vector<ShaderBinary> m_shaders;

    virtual Result createShaderModule(
        slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode) override;
};

} // namespace d3d12
} // namespace rhi
