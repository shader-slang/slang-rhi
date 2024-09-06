#pragma once

#include "../command-encoder-com-forward.h"
#include "../mutable-shader-object.h"
#include "../renderer-shared.h"
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
class SamplerImpl;
class ResourceViewImpl;
class TextureViewImpl;
class TexelBufferViewImpl;
class PlainBufferViewImpl;
class AccelerationStructureImpl;
class RenderPassLayoutImpl;
class PipelineImpl;
class RayTracingPipelineImpl;
class ShaderObjectLayoutImpl;
class EntryPointLayout;
class RootShaderObjectLayout;
class ShaderProgramImpl;
class CommandEncoderImpl;
class ShaderObjectImpl;
class MutableShaderObjectImpl;
class RootShaderObjectImpl;
class MutableRootShaderObjectImpl;
class ShaderTableImpl;
class ResourceCommandEncoderImpl;
class RenderCommandEncoderImpl;
class ComputeCommandEncoderImpl;
class RayTracingCommandEncoder;
class CommandBufferImpl;
class CommandQueueImpl;
class TransientResourceHeapImpl;
class QueryPoolImpl;
class SwapchainImpl;

} // namespace rhi::vk
