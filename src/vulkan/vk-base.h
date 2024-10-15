#pragma once

#include "../mutable-shader-object.h"
#include "../rhi-shared.h"
#include "../transient-resource-heap-base.h"
#include "vk-api.h"
#include "vk-descriptor-allocator.h"
#include "vk-device-queue.h"

#include "core/common.h"

namespace rhi::vk {

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
class RootShaderObjectLayout;
class ShaderProgramImpl;
class ShaderObjectImpl;
class MutableShaderObjectImpl;
class RootShaderObjectImpl;
class MutableRootShaderObjectImpl;
class ShaderTableImpl;
class CommandEncoderImpl;
class CommandBufferImpl;
class CommandQueueImpl;
class TransientResourceHeapImpl;
class QueryPoolImpl;

} // namespace rhi::vk
