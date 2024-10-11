#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugInputLayout : public DebugObject<IInputLayout>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugInputLayout);

public:
    IInputLayout* getInterface(const Guid& guid);
};

} // namespace rhi::debug
