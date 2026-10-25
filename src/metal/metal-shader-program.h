#pragma once

#include "metal-base.h"

#include <string>

namespace rhi::metal {

class ShaderProgramImpl : public ShaderProgram
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;

    struct Module
    {
        SlangStage stage;
        std::string entryPointName;
        ComPtr<ISlangBlob> code;
        NS::SharedPtr<MTL::Library> library;

        /// The entry point as a function a pipeline can be built from. A function that uses
        /// function constants (Slang emits one per specialization constant) has to be specialized
        /// first; it is specialized with no values, so each constant takes the default Slang
        /// emitted for it, as on the other backends.
        Result createFunction(DeviceImpl* device, NS::SharedPtr<MTL::Function>& outFunction) const;
    };

    std::vector<Module> m_modules;

    ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc);
    ~ShaderProgramImpl();

    virtual Result createShaderModule(const ShaderModuleDesc& desc, ComPtr<ISlangBlob> kernelCode) override;

    virtual ShaderObjectLayout* getRootShaderObjectLayout() override;
};

} // namespace rhi::metal
