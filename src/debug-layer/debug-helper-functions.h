#pragma once
#include "debug-base.h"
#include "debug-buffer.h"
#include "debug-command-buffer.h"
#include "debug-command-queue.h"
#include "debug-device.h"
#include "debug-fence.h"
#include "debug-pipeline.h"
#include "debug-query.h"
#include "debug-sampler.h"
#include "debug-shader-object.h"
#include "debug-shader-program.h"
#include "debug-shader-table.h"
#include "debug-surface.h"
#include "debug-texture.h"
#include "debug-texture-view.h"
#include "debug-transient-heap.h"
#include "debug-input-layout.h"

#include <vector>

namespace rhi::debug {

#ifdef __FUNCSIG__
#define SLANG_FUNC_SIG __FUNCSIG__
#elif defined(__PRETTY_FUNCTION__)
#define SLANG_FUNC_SIG __FUNCSIG__
#elif defined(__FUNCTION__)
#define SLANG_FUNC_SIG __FUNCTION__
#else
#define SLANG_FUNC_SIG "UnknownFunction"
#endif

extern thread_local const char* _currentFunctionName;
struct SetCurrentFuncRAII
{
    SetCurrentFuncRAII(const char* funcName) { _currentFunctionName = funcName; }
    ~SetCurrentFuncRAII() { _currentFunctionName = nullptr; }
};
#define SLANG_RHI_API_FUNC SetCurrentFuncRAII setFuncNameRAII(SLANG_FUNC_SIG)
#define SLANG_RHI_API_FUNC_NAME(x) SetCurrentFuncRAII setFuncNameRAII(x)

/// Returns the public API function name from a `SLANG_FUNC_SIG` string.
std::string _rhiGetFuncName(const char* input);

template<typename... TArgs>
char* _rhiDiagnoseFormat(
    char* buffer,                   // Initial buffer to output formatted string.
    size_t shortBufferSize,         // Size of the initial buffer.
    std::vector<char>& bufferArray, // A list for allocating a large buffer if needed.
    const char* format,             // The format string.
    TArgs... args
)
{
    int length = snprintf(buffer, shortBufferSize, format, args...);
    if (length < 0)
        return buffer;
    if (length > 255)
    {
        bufferArray.resize(length + 1);
        buffer = bufferArray.data();
        snprintf(buffer, bufferArray.size(), format, args...);
    }
    return buffer;
}

template<typename... TArgs>
void _rhiDiagnoseImpl(DebugContext* ctx, DebugMessageType type, const char* format, TArgs... args)
{
    char shortBuffer[256];
    std::vector<char> bufferArray;
    auto buffer = _rhiDiagnoseFormat(shortBuffer, sizeof(shortBuffer), bufferArray, format, args...);
    ctx->debugCallback->handleMessage(type, DebugMessageSource::Layer, buffer);
}

#define RHI_VALIDATION_ERROR(message)                                                                                  \
    _rhiDiagnoseImpl(                                                                                                  \
        ctx,                                                                                                           \
        DebugMessageType::Error,                                                                                       \
        "%s: %s",                                                                                                      \
        _rhiGetFuncName(_currentFunctionName ? _currentFunctionName : SLANG_FUNC_SIG).c_str(),                         \
        message                                                                                                        \
    )
#define RHI_VALIDATION_WARNING(message)                                                                                \
    _rhiDiagnoseImpl(                                                                                                  \
        ctx,                                                                                                           \
        DebugMessageType::Warning,                                                                                     \
        "%s: %s",                                                                                                      \
        _rhiGetFuncName(_currentFunctionName ? _currentFunctionName : SLANG_FUNC_SIG).c_str(),                         \
        message                                                                                                        \
    )
#define RHI_VALIDATION_INFO(message)                                                                                   \
    _rhiDiagnoseImpl(                                                                                                  \
        ctx,                                                                                                           \
        DebugMessageType::Info,                                                                                        \
        "%s: %s",                                                                                                      \
        _rhiGetFuncName(_currentFunctionName ? _currentFunctionName : SLANG_FUNC_SIG).c_str(),                         \
        message                                                                                                        \
    )
#define RHI_VALIDATION_FORMAT(type, format, ...)                                                                       \
    {                                                                                                                  \
        char shortBuffer[256];                                                                                         \
        std::vector<char> bufferArray;                                                                                 \
        auto message = _rhiDiagnoseFormat(shortBuffer, sizeof(shortBuffer), bufferArray, format, __VA_ARGS__);         \
        _rhiDiagnoseImpl(                                                                                              \
            ctx,                                                                                                       \
            type,                                                                                                      \
            "%s: %s",                                                                                                  \
            _rhiGetFuncName(_currentFunctionName ? _currentFunctionName : SLANG_FUNC_SIG).c_str(),                     \
            message                                                                                                    \
        );                                                                                                             \
    }
#define RHI_VALIDATION_ERROR_FORMAT(...) RHI_VALIDATION_FORMAT(DebugMessageType::Error, __VA_ARGS__)

#define SLANG_RHI_DEBUG_GET_INTERFACE_IMPL(typeName)                                                                   \
    I##typeName* Debug##typeName::getInterface(const Guid& guid)                                                       \
    {                                                                                                                  \
        return (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_I##typeName) ? static_cast<I##typeName*>(this)    \
                                                                                  : nullptr;                           \
    }
#define SLANG_RHI_DEBUG_GET_INTERFACE_IMPL_PARENT(typeName, parentType)                                                \
    I##typeName* Debug##typeName::getInterface(const Guid& guid)                                                       \
    {                                                                                                                  \
        return (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_I##typeName || guid == GUID::IID_I##parentType)   \
                   ? static_cast<I##typeName*>(this)                                                                   \
                   : nullptr;                                                                                          \
    }

// Utility conversion functions to get Debug* object or the inner object from a user provided
// pointer.
#define SLANG_RHI_DEBUG_GET_OBJ_IMPL(type)                                                                             \
    inline Debug##type* getDebugObj(I##type* ptr)                                                                      \
    {                                                                                                                  \
        return checked_cast<Debug##type*>(checked_cast<DebugObject<I##type>*>(ptr));                                   \
    }                                                                                                                  \
    inline I##type* getInnerObj(I##type* ptr)                                                                          \
    {                                                                                                                  \
        if (!ptr)                                                                                                      \
            return nullptr;                                                                                            \
        auto debugObj = getDebugObj(ptr);                                                                              \
        return debugObj->baseObject;                                                                                   \
    }

#define SLANG_RHI_DEBUG_GET_OBJ_IMPL_UNOWNED(type)                                                                     \
    inline Debug##type* getDebugObj(I##type* ptr)                                                                      \
    {                                                                                                                  \
        return checked_cast<Debug##type*>(checked_cast<UnownedDebugObject<I##type>*>(ptr));                            \
    }                                                                                                                  \
    inline I##type* getInnerObj(I##type* ptr)                                                                          \
    {                                                                                                                  \
        if (!ptr)                                                                                                      \
            return nullptr;                                                                                            \
        auto debugObj = getDebugObj(ptr);                                                                              \
        return debugObj->baseObject;                                                                                   \
    }

SLANG_RHI_DEBUG_GET_OBJ_IMPL(Device)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(Buffer)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(Texture)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(TextureView)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(CommandBuffer)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(CommandQueue)
SLANG_RHI_DEBUG_GET_OBJ_IMPL_UNOWNED(ResourcePassEncoder)
SLANG_RHI_DEBUG_GET_OBJ_IMPL_UNOWNED(RenderPassEncoder)
SLANG_RHI_DEBUG_GET_OBJ_IMPL_UNOWNED(ComputePassEncoder)
SLANG_RHI_DEBUG_GET_OBJ_IMPL_UNOWNED(RayTracingPassEncoder)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(InputLayout)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(Pipeline)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(RenderPipeline)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(ComputePipeline)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(RayTracingPipeline)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(Sampler)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(ShaderObject)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(ShaderProgram)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(Surface)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(TransientResourceHeap)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(QueryPool)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(AccelerationStructure)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(Fence)
SLANG_RHI_DEBUG_GET_OBJ_IMPL(ShaderTable)

inline BufferWithOffset getInnerObj(BufferWithOffset buffer)
{
    return BufferWithOffset{getInnerObj(buffer.buffer), buffer.offset};
}

std::string createBufferLabel(const BufferDesc& desc);
std::string createTextureLabel(const TextureDesc& desc);
std::string createSamplerLabel(const SamplerDesc& desc);

void validateAccelerationStructureBuildDesc(DebugContext* ctx, const AccelerationStructureBuildDesc& buildDesc);

} // namespace rhi::debug
