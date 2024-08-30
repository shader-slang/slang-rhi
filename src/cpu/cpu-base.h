#pragma once

#include "../immediate-renderer-base.h"
#include "../mutable-shader-object.h"
#include "../slang-context.h"
#define SLANG_PRELUDE_NAMESPACE slang_prelude
#include "prelude/slang-cpp-types.h"

#include "core/common.h"

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>

namespace rhi::cpu {

class BufferResourceImpl;
class TextureResourceImpl;
class ResourceViewImpl;
class BufferResourceViewImpl;
class TextureResourceViewImpl;
class ShaderObjectLayoutImpl;
class EntryPointLayoutImpl;
class RootShaderObjectLayoutImpl;
class ShaderObjectImpl;
class MutableShaderObjectImpl;
class EntryPointShaderObjectImpl;
class RootShaderObjectImpl;
class ShaderProgramImpl;
class PipelineStateImpl;
class QueryPoolImpl;
class DeviceImpl;

} // namespace rhi::cpu
