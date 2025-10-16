#pragma once

#include "../rhi-shared.h"
#include "d3d12-api.h"
#include "d3d12-resource.h"

#include "core/common.h"

// Must be included after d3d12 headers.
#include "../nvapi/nvapi-util.h"
#include "d3d12-descriptor-heap.h"

namespace rhi::d3d12 {

class AdapterImpl;
class DeviceImpl;
class BufferImpl;
class TextureImpl;
class TextureViewImpl;
class CommandEncoderImpl;
class CommandBufferImpl;
class CommandQueueImpl;
class FenceImpl;
class QueryPoolImpl;
class PlainBufferProxyQueryPoolImpl;
class RenderPipelineImpl;
class ComputePipelineImpl;
class RayTracingPipelineImpl;
class AccelerationStructureImpl;
class SamplerImpl;
class ShaderObjectImpl;
class RootShaderObjectImpl;
class ShaderObjectLayoutImpl;
class RootShaderObjectLayoutImpl;
class ShaderProgramImpl;
class ShaderTableImpl;
class SurfaceImpl;
class InputLayoutImpl;
struct BindingDataImpl;
struct BindingCache;

} // namespace rhi::d3d12
