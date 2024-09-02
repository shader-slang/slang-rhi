#pragma once

#include "../d3d/d3d-swapchain.h"
#include "../d3d/d3d-util.h"
#include "../flag-combiner.h"
#include "../immediate-renderer-base.h"
#include "../mutable-shader-object.h"
#include "../nvapi/nvapi-util.h"

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

#if SLANG_RHI_ENABLE_NVAPI
// NVAPI integration is described here
// https://developer.nvidia.com/unlocking-gpu-intrinsics-hlsl

#include "../nvapi/nvapi-include.h"
#endif

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
class ResourceViewImpl;
class ShaderResourceViewImpl;
class UnorderedAccessViewImpl;
class DepthStencilViewImpl;
class RenderTargetViewImpl;
class FramebufferLayoutImpl;
class FramebufferImpl;
class SwapchainImpl;
class InputLayoutImpl;
class QueryPoolImpl;
class PipelineImpl;
class GraphicsPipelineImpl;
class ComputePipelineImpl;
class ShaderObjectLayoutImpl;
class RootShaderObjectLayoutImpl;
class ShaderObjectImpl;
class MutableShaderObjectImpl;
class RootShaderObjectImpl;

} // namespace rhi::d3d11
