#pragma once

#include "../rhi-shared.h"
#define SLANG_PRELUDE_NAMESPACE slang_prelude
#include "prelude/slang-cpp-types.h"

#include "core/common.h"

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>

namespace rhi::cpu {

class BufferImpl;
class TextureImpl;
class TextureViewImpl;
class ShaderObjectLayoutImpl;
class EntryPointLayoutImpl;
class RootShaderObjectLayoutImpl;
class ShaderObjectImpl;
class EntryPointShaderObjectImpl;
class RootShaderObjectImpl;
class ShaderProgramImpl;
class ComputePipelineImpl;
class QueryPoolImpl;
class CommandQueueImpl;
class CommandEncoderImpl;
class CommandBufferImpl;
class DeviceImpl;

} // namespace rhi::cpu
