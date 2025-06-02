#pragma once

#include "cpu-base.h"

namespace rhi::cpu {

class ShaderProgramImpl : public ShaderProgram
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootShaderObjectLayout;

    ShaderProgramImpl(Device* device)
        : ShaderProgram(device)
    {
    }
    ~ShaderProgramImpl() {}

    virtual ShaderObjectLayout* getRootShaderObjectLayout() override;
};

} // namespace rhi::cpu
