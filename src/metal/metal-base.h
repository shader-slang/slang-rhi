#pragma once

#include "../pass-encoder-com-forward.h"
#include "../mutable-shader-object.h"
#include "../rhi-shared.h"
#include "../transient-resource-heap-base.h"
#include "metal-api.h"

#include "core/common.h"

namespace rhi::metal {

class DeviceImpl;
class InputLayoutImpl;
class BufferImpl;
class FenceImpl;
class TextureImpl;
class SamplerImpl;
class TextureViewImpl;
class AccelerationStructureImpl;
class RenderPipelineImpl;
class ComputePipelineImpl;
class RayTracingPipelineImpl;
class ShaderObjectLayoutImpl;
class EntryPointLayout;
class RootShaderObjectLayoutImpl;
class ShaderProgramImpl;
class PassEncoderImpl;
class ShaderObjectImpl;
class MutableShaderObjectImpl;
class RootShaderObjectImpl;
class ShaderTableImpl;
class ResourcePassEncoderImpl;
class RenderPassEncoderImpl;
class ComputePassEncoderImpl;
class RayTracingPassEncoderImpl;
class CommandBufferImpl;
class CommandQueueImpl;
class TransientResourceHeapImpl;
class QueryPoolImpl;

} // namespace rhi::metal
