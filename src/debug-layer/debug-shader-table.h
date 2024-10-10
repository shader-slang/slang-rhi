#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugShaderTable : public DebugObject<IShaderTable>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugShaderTable);

public:
    IShaderTable* getInterface(const Guid& guid);
};

} // namespace rhi::debug
