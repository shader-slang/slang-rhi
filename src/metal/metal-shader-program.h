// metal-shader-program.h
#pragma once

#include "metal-base.h"
#include "metal-shader-object-layout.h"

#include <string>

namespace gfx
{

using namespace Slang;

namespace metal
{

class ShaderProgramImpl : public ShaderProgramBase
{
public:
    DeviceImpl* m_device;
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;

    struct Module
    {
        SlangStage stage;
        std::string entryPointName;
        ComPtr<ISlangBlob> code;
        NS::SharedPtr<MTL::Library> library;
    };

    std::vector<Module> m_modules;

    ShaderProgramImpl(DeviceImpl* device);
    ~ShaderProgramImpl();

    virtual Result createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode) override;
};


} // namespace metal
} // namespace gfx
