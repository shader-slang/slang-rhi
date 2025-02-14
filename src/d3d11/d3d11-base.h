#pragma once

#include "../rhi-shared.h"
#include "../d3d/d3d-surface.h"
#include "../d3d/d3d-util.h"
#include "../flag-combiner.h"

#include "core/common.h"

#include <slang-com-ptr.h>

#pragma push_macro("WIN32_LEAN_AND_MEAN")
#pragma push_macro("NOMINMAX")
#undef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#undef NOMINMAX
#define NOMINMAX
#include <windows.h>
#pragma pop_macro("NOMINMAX")
#pragma pop_macro("WIN32_LEAN_AND_MEAN")

#include <d3d11_2.h>
#include <d3dcompiler.h>

// Must be included after d3d11 headers.
#include "../nvapi/nvapi-util.h"

// We will use the C standard library just for printing error messages.
#include <stdio.h>

#ifdef _MSC_VER
#include <stddef.h>
#if (_MSC_VER < 1900)
#define snprintf sprintf_s
#endif
#endif

namespace rhi::d3d11 {

class DeviceImpl;
class ShaderProgramImpl;
class BufferImpl;
class TextureImpl;
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
