#pragma once

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
class TextureViewImpl;
class SamplerImpl;
class AccelerationStructureImpl;
class RenderPipelineImpl;
class ComputePipelineImpl;
class RayTracingPipelineImpl;
class ShaderObjectLayoutImpl;
class EntryPointLayout;
class RootShaderObjectLayoutImpl;
class ShaderProgramImpl;
class ShaderObjectImpl;
class MutableShaderObjectImpl;
class RootShaderObjectImpl;
class ShaderTableImpl;
class CommandEncoderImpl;
class CommandBufferImpl;
class CommandQueueImpl;
class TransientResourceHeapImpl;
class QueryPoolImpl;

} // namespace rhi::metal
