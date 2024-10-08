#pragma once

#include "cuda-api.h"

#include "../pass-encoder-com-forward.h"
#include "../command-writer.h"
#include "../mutable-shader-object.h"
#include "../rhi-shared.h"
#include "../simple-transient-resource-heap.h"
#include "../slang-context.h"

#include "core/common.h"

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>

namespace rhi::cuda {

class BufferImpl;
class TextureImpl;
class ResourceViewImpl;
class ShaderObjectLayoutImpl;
class RootShaderObjectLayoutImpl;
class ShaderObjectImpl;
class MutableShaderObjectImpl;
class EntryPointShaderObjectImpl;
class RootShaderObjectImpl;
class ShaderProgramImpl;
class PipelineImpl;
class QueryPoolImpl;
class DeviceImpl;
class CommandBufferImpl;
class PassEncoderImpl;
class ResourcePassEncoderImpl;
class ComputePassEncoderImpl;
class CommandQueueImpl;
class AccelerationStructureImpl;

} // namespace rhi::cuda
