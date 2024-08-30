#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugFramebuffer : public DebugObject<IFramebuffer>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    IFramebuffer* getInterface(const Guid& guid);
};

class DebugFramebufferLayout : public DebugObject<IFramebufferLayout>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    IFramebufferLayout* getInterface(const Guid& guid);
};

} // namespace rhi::debug
