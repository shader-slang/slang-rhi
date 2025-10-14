#pragma once

#include "cuda-api.h"
#include "optix-api.h"

#include "../rhi-shared.h"
#include "../slang-context.h"

#include "core/common.h"

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>

namespace rhi::cuda {

class AdapterImpl;
class BufferImpl;
class TextureImpl;
class TextureViewImpl;
class SamplerImpl;
class ShaderObjectLayoutImpl;
class RootShaderObjectLayoutImpl;
class ShaderProgramImpl;
class ComputePipelineImpl;
class RayTracingPipelineImpl;
class QueryPoolImpl;
class DeviceImpl;
class CommandBufferImpl;
class CommandEncoderImpl;
class CommandQueueImpl;
class AccelerationStructureImpl;
class ShaderTableImpl;
class HeapImpl;
struct BindingDataImpl;
struct BindingCache;

} // namespace rhi::cuda
