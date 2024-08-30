#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugInputLayout : public DebugObject<IInputLayout>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    IInputLayout* getInterface(const Guid& guid);
};

} // namespace rhi::debug
