#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugShaderProgram : public DebugObject<IShaderProgram>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    IShaderProgram* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW slang::TypeReflection* SLANG_MCALL findTypeByName(const char* name) override;

public:
    ComPtr<slang::IComponentType> m_slangProgram;
};

} // namespace rhi::debug
