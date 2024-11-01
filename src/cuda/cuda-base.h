#pragma once

#include "cuda-api.h"

#include "../rhi-shared.h"
#include "../slang-context.h"

#include "core/common.h"

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>

namespace rhi::cuda {

class BufferImpl;
class TextureImpl;
class TextureViewImpl;
class ShaderObjectLayoutImpl;
class RootShaderObjectLayoutImpl;
class ShaderObjectImpl;
class EntryPointShaderObjectImpl;
class RootShaderObjectImpl;
class ShaderProgramImpl;
class ComputePipelineImpl;
class QueryPoolImpl;
class DeviceImpl;
class CommandBufferImpl;
class CommandEncoderImpl;
class CommandQueueImpl;
class AccelerationStructureImpl;

} // namespace rhi::cuda
