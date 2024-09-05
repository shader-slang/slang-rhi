#pragma once

#include <slang-com-ptr.h>
#include <slang-rhi.h>

#include "../command-encoder-com-forward.h"
#include "../renderer-shared.h"

#include "core/common.h"

namespace rhi::debug {

class DebugObjectBase : public ComObject
{
public:
    uint64_t uid;
    DebugObjectBase()
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
};

template<typename TInterface>
class UnownedDebugObject : public TInterface, public DebugObjectBase
{
public:
    TInterface* baseObject = nullptr;
};

class DebugDevice;
class DebugShaderTable;
class DebugQueryPool;
class DebugBuffer;
class DebugTexture;
class DebugResourceView;
class DebugAccelerationStructure;
class DebugSampler;
class DebugShaderObject;
class DebugRootShaderObject;
class DebugCommandBuffer;
template<typename TEncoderInterface>
class DebugCommandEncoderImpl;
class DebugResourceCommandEncoder;
class DebugRenderCommandEncoder;
class DebugComputeCommandEncoder;
class DebugRayTracingCommandEncoder;
class DebugFence;
class DebugCommandQueue;
class DebugFramebuffer;
class DebugFramebufferLayout;
class DebugInputLayout;
class DebugPipeline;
class DebugRenderPassLayout;
class DebugShaderProgram;
class DebugTransientResourceHeap;
class DebugSwapchain;

} // namespace rhi::debug
