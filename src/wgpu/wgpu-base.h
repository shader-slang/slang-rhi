#pragma once

#include "wgpu-api.h"
#include "../rhi-shared.h"

#include "core/common.h"

namespace rhi::wgpu {

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

} // namespace rhi::wgpu
