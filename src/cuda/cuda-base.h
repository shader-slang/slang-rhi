// cuda-base.h
// Shared header file for CUDA implementation
#pragma once

#include "cuda-api.h"

#include "slang.h"
#include "slang-com-ptr.h"
#include "slang-com-helper.h"
#include "../command-writer.h"
#include "../renderer-shared.h"
#include "../mutable-shader-object.h"
#include "../simple-transient-resource-heap.h"
#include "../slang-context.h"
#include "../command-encoder-com-forward.h"

#   ifdef RENDER_TEST_OPTIX

// The `optix_stubs.h` header produces warnings when compiled with MSVC
#       ifdef _MSC_VER
#           pragma warning(disable: 4996)
#       endif

#       include <optix.h>
#       include <optix_function_table_definition.h>
#       include <optix_stubs.h>
#   endif

#include "utils/common.h"

namespace rhi
{
namespace cuda
{
    class CUDAContext;
    class BufferResourceImpl;
    class TextureResourceImpl;
    class ResourceViewImpl;
    class ShaderObjectLayoutImpl;
    class RootShaderObjectLayoutImpl;
    class ShaderObjectImpl;
    class MutableShaderObjectImpl;
    class EntryPointShaderObjectImpl;
    class RootShaderObjectImpl;
    class ShaderProgramImpl;
    class PipelineStateImpl;
    class QueryPoolImpl;
    class DeviceImpl;
    class CommandBufferImpl;
    class ResourceCommandEncoderImpl;
    class ComputeCommandEncoderImpl;
    class CommandQueueImpl;
}
}
