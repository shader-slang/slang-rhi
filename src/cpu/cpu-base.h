#pragma once

#include "../immediate-device.h"
#include "../mutable-shader-object.h"
#include "../slang-context.h"
#define SLANG_PRELUDE_NAMESPACE slang_prelude
#include "prelude/slang-cpp-types.h"

#include "core/common.h"

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>

namespace rhi::cpu {

class BufferImpl;
class TextureImpl;
class ResourceViewImpl;
class BufferViewImpl;
class TextureViewImpl;
class ShaderObjectLayoutImpl;
class EntryPointLayoutImpl;
class RootShaderObjectLayoutImpl;
class ShaderObjectImpl;
class MutableShaderObjectImpl;
class EntryPointShaderObjectImpl;
class RootShaderObjectImpl;
class ShaderProgramImpl;
class PipelineImpl;
class QueryPoolImpl;
class DeviceImpl;

} // namespace rhi::cpu
