#pragma once

#include "cuda-api.h"

#include "../rhi-shared.h"
#include "../slang-context.h"

#include "core/common.h"

namespace rhi::cuda {

class BufferImpl;
class TextureImpl;
class TextureViewImpl;
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
struct BindingDataImpl;
struct BindingCache;

} // namespace rhi::cuda
