#pragma once

#include <slang-com-ptr.h>
#include <slang-rhi.h>

#include "../rhi-shared.h"

#include "core/common.h"

namespace rhi::debug {

struct DebugContext
{
    IDebugCallback* debugCallback = nullptr;
};

class DebugObjectBase : public ComObject
{
public:
    uint64_t uid;
    DebugContext* ctx;

    DebugObjectBase(DebugContext* ctx)
        : ctx(ctx)
    {
        static uint64_t uidCounter = 0;
        uid = ++uidCounter;
    }
};

template<typename TInterface>
class DebugObject : public TInterface, public DebugObjectBase
{
public:
    ComPtr<TInterface> baseObject;

    DebugObject(DebugContext* ctx)
        : DebugObjectBase(ctx)
    {
    }
};

#define SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(name)                                                                       \
    name(DebugContext* ctx)                                                                                            \
        : DebugObject(ctx)                                                                                             \
    {                                                                                                                  \
    }

template<typename TInterface>
class UnownedDebugObject : public TInterface, public DebugObjectBase
{
public:
    TInterface* baseObject = nullptr;

    UnownedDebugObject(DebugContext* ctx)
        : DebugObjectBase(ctx)
    {
    }
};

#define SLANG_RHI_UNOWNED_DEBUG_OBJECT_CONSTRUCTOR(name)                                                               \
    name(DebugContext* ctx)                                                                                            \
        : UnownedDebugObject(ctx)                                                                                      \
    {                                                                                                                  \
    }

class DebugDevice;
class DebugQueryPool;
class DebugShaderObject;
class DebugRootShaderObject;
class DebugCommandBuffer;
class DebugCommandEncoder;
class DebugRenderPassEncoder;
class DebugComputePassEncoder;
class DebugRayTracingPassEncoder;
class DebugFence;
class DebugCommandQueue;
class DebugTransientResourceHeap;
class DebugSurface;

} // namespace rhi::debug
