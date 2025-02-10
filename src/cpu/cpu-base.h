#pragma once

#include "../rhi-shared.h"

#include "core/common.h"

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>

#define SLANG_PRELUDE_NAMESPACE slang_prelude
#include <slang-cpp-types.h>

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
struct BindingDataImpl;
struct BindingCache;

} // namespace rhi::cpu
