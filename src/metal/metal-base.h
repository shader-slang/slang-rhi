// metal-base.h
// Shared header file for Metal implementation.
#pragma once

#include "../command-encoder-com-forward.h"
#include "../mutable-shader-object.h"
#include "../renderer-shared.h"
#include "../transient-resource-heap-base.h"
#include "metal-api.h"

#include "utils/common.h"

namespace rhi
{
namespace metal
{

    class DeviceImpl;
    class InputLayoutImpl;
    class BufferResourceImpl;
    class FenceImpl;
    class TextureResourceImpl;
    class SamplerStateImpl;
    class ResourceViewImpl;
    class BufferResourceViewImpl;
    class TextureResourceViewImpl;
    class TexelBufferResourceViewImpl;
    class PlainBufferResourceViewImpl;
    class AccelerationStructureImpl;
    class FramebufferLayoutImpl;
    class RenderPassLayoutImpl;
    class FramebufferImpl;
    class PipelineStateImpl;
    class RayTracingPipelineStateImpl;
    class ShaderObjectLayoutImpl;
    class EntryPointLayout;
    class RootShaderObjectLayoutImpl;
    class ShaderProgramImpl;
    class PipelineCommandEncoder;
    class ShaderObjectImpl;
    class MutableShaderObjectImpl;
    class RootShaderObjectImpl;
    class ShaderTableImpl;
    class ResourceCommandEncoder;
    class RenderCommandEncoder;
    class ComputeCommandEncoder;
    class RayTracingCommandEncoder;
    class CommandBufferImpl;
    class CommandQueueImpl;
    class TransientResourceHeapImpl;
    class QueryPoolImpl;
    class SwapchainImpl;

} // namespace metal
} // namespace rhi
