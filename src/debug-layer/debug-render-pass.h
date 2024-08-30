#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugRenderPassLayout : public DebugObject<IRenderPassLayout>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    IRenderPassLayout* getInterface(const Guid& guid);
};

} // namespace rhi::debug
