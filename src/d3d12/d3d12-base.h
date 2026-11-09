#pragma once

#include "../rhi-shared.h"
#include "d3d12-api.h"
#include "d3d12-resource.h"

// Must be included after d3d12 headers.
#include "../nvapi/nvapi-util.h"
#include "d3d12-descriptor-heap.h"

#include "aftermath.h"

#include "core/common.h"

namespace rhi::d3d12 {

// DXR permits 4064 application bytes, but a local root constant parameter above 128 DWORDs
// causes device removal on the tested RTX 5090 / 610.88 driver. Keep a temporary backend-wide
// cap until that driver blocker is resolved. This excludes the shader identifier and padding.
inline constexpr uint32_t kMaxShaderRecordDataSize = 512;
inline constexpr const char* kShaderRecordDataLimitReason =
    "D3D12 driver blocker: larger local root constants can cause device removal";

class BackendImpl;
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
class MicromapImpl;
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
