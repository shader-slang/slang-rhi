#pragma once

#include "../rhi-shared.h"
#include "d3d11-api.h"

// Must be included after d3d11 headers.
#include "../nvapi/nvapi-util.h"

#include "core/common.h"

namespace rhi::d3d11 {

class AdapterImpl;
class DeviceImpl;
class ShaderProgramImpl;
class BufferImpl;
class TextureImpl;
class TextureViewImpl;
class SamplerImpl;
class SurfaceImpl;
class InputLayoutImpl;
class QueryPoolImpl;
class RenderPipelineImpl;
class ComputePipelineImpl;
class ShaderObjectLayoutImpl;
class RootShaderObjectLayoutImpl;
class CommandQueueImpl;
class CommandEncoderImpl;
class CommandBufferImpl;
struct BindingDataImpl;
struct BindingCache;

} // namespace rhi::d3d11
