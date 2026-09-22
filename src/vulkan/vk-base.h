#pragma once

#include "../rhi-shared.h"
#include "vk-api.h"
#include "vk-descriptor-allocator.h"
#include "vk-device-queue.h"

#include "aftermath.h"

#include "core/common.h"

namespace rhi::vk {

/// Queue-family ownership state of a shared (Buffer/TextureUsage::Shared) resource in the
/// producer <-> VK_QUEUE_FAMILY_EXTERNAL (e.g. CUDA) ping-pong tracked by DeviceImpl's registry.
enum class SharedOwnershipState
{
    OwnedByProducer,
    ReleasedToExternal,
};

class BackendImpl;
class AdapterImpl;
class DeviceImpl;
class InputLayoutImpl;
class BufferImpl;
class FenceImpl;
class TextureImpl;
class TextureViewImpl;
class SamplerImpl;
class AccelerationStructureImpl;
class MicromapImpl;
class RenderPipelineImpl;
class ComputePipelineImpl;
class RayTracingPipelineImpl;
class ShaderObjectLayoutImpl;
class EntryPointLayout;
class RootShaderObjectLayoutImpl;
class ShaderProgramImpl;
class ShaderTableImpl;
class CommandEncoderImpl;
class CommandBufferImpl;
class CommandQueueImpl;
class QueryPoolImpl;
struct BindingDataImpl;
struct BindingCache;

} // namespace rhi::vk
