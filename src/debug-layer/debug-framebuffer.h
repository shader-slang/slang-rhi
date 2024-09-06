#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugFramebufferLayout : public DebugObject<IFramebufferLayout>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    IFramebufferLayout* getInterface(const Guid& guid);
};

} // namespace rhi::debug
