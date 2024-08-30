#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugShaderTable : public DebugObject<IShaderTable>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;
    IShaderTable* getInterface(const Slang::Guid& guid);
};

} // namespace rhi::debug
