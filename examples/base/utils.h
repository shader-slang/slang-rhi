#pragma once

#include "linalg.h"
using namespace linalg::aliases;

#include <slang-rhi.h>
using namespace rhi;

#include <execution>
#include <limits>

// ---------------------------------------------------------------------------------------
// Math
// ---------------------------------------------------------------------------------------

static constexpr float kPI = 3.14159265358979323846f;

inline float radians(float degrees)
{
    return degrees * kPI / 180.0f;
}

inline float degrees(float radians)
{
    return radians * 180.0f / kPI;
}

template<typename T>
T divRoundUp(T x, T y)
{
    return (x + (y - 1)) / y;
}

// ---------------------------------------------------------------------------------------
// Program and pipeline creation helpers
// ---------------------------------------------------------------------------------------

#define PRINT_DIAGNOSTICS(diagnostics)                                                                                 \
    {                                                                                                                  \
        if (diagnostics)                                                                                               \
        {                                                                                                              \
            const char* msg = (const char*)diagnostics->getBufferPointer();                                            \
            printf("%s\n", msg);                                                                                       \
        }                                                                                                              \
    }

inline Result createProgram(
    IDevice* device,
    const char* pathOrSource,
    bool isSource,
    const char** entryPointNames,
    size_t entryPointCount,
    IShaderProgram** outProgram
)
{
    ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = nullptr;
    if (isSource)
    {
        module = device->getSlangSession()
                     ->loadModuleFromSourceString(nullptr, nullptr, pathOrSource, diagnostics.writeRef());
    }
    else
    {
        module = device->getSlangSession()->loadModule(pathOrSource, diagnostics.writeRef());
    }
    PRINT_DIAGNOSTICS(diagnostics);
    if (!module)
    {
        printf("Failed to load Slang module from '%s'\n", pathOrSource);
        return SLANG_FAIL;
    }
    std::vector<slang::IComponentType*> entryPoints;
    for (size_t i = 0; i < entryPointCount; ++i)
    {
        slang::IEntryPoint* entryPoint;
        if (!SLANG_SUCCEEDED(module->findEntryPointByName(entryPointNames[i], &entryPoint)))
        {
            printf("Failed to find entry point '%s' in module '%s'\n", entryPointNames[i], pathOrSource);
            return SLANG_FAIL;
        }
        entryPoints.push_back(entryPoint);
    }
    ShaderProgramDesc programDesc = {};
    programDesc.linkingStyle = LinkingStyle::SingleProgram;
    programDesc.slangEntryPoints = entryPoints.data();
    programDesc.slangEntryPointCount = entryPoints.size();
    programDesc.slangGlobalScope = module;
    device->createShaderProgram(programDesc, outProgram, diagnostics.writeRef());
    PRINT_DIAGNOSTICS(diagnostics);
    if (!(*outProgram))
    {
        printf("Failed to create program for module '%s'\n", pathOrSource);
        return SLANG_FAIL;
    }
    return SLANG_OK;
}

inline Result _createComputePipeline(
    IDevice* device,
    const char* pathOrSource,
    bool isSource,
    const char* entryPointName,
    IComputePipeline** outPipeline
)
{
    ComPtr<IShaderProgram> program;
    SLANG_RETURN_ON_FAIL(createProgram(device, pathOrSource, isSource, &entryPointName, 1, program.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program;
    SLANG_RETURN_ON_FAIL(device->createComputePipeline(pipelineDesc, outPipeline));
    return SLANG_OK;
}

inline Result createComputePipeline(
    IDevice* device,
    const char* path,
    const char* entryPointName,
    IComputePipeline** outPipeline
)
{
    return _createComputePipeline(device, path, false, entryPointName, outPipeline);
}

inline Result createComputePipelineFromSource(
    IDevice* device,
    const char* source,
    const char* entryPointName,
    IComputePipeline** outPipeline
)
{
    return _createComputePipeline(device, source, true, entryPointName, outPipeline);
}

inline Result _createRenderPipeline(
    IDevice* device,
    const char* pathOrSource,
    bool isSource,
    const char* vertexEntryPointName,
    const char* fragmentEntryPointName,
    const RenderPipelineDesc& pipelineDesc,
    IRenderPipeline** outPipeline
)
{
    ComPtr<IShaderProgram> program;
    const char* entryPointNames[] = {vertexEntryPointName, fragmentEntryPointName};
    SLANG_RETURN_ON_FAIL(createProgram(
        device,
        pathOrSource,
        isSource,
        entryPointNames,
        SLANG_COUNT_OF(entryPointNames),
        program.writeRef()
    ));

    RenderPipelineDesc pipelineDescCopy = pipelineDesc;
    pipelineDescCopy.program = program;
    SLANG_RETURN_ON_FAIL(device->createRenderPipeline(pipelineDescCopy, outPipeline));
    return SLANG_OK;
}

inline Result createRenderPipeline(
    IDevice* device,
    const char* path,
    const char* vertexEntryPointName,
    const char* fragmentEntryPointName,
    const RenderPipelineDesc& pipelineDesc,
    IRenderPipeline** outPipeline
)
{
    return _createRenderPipeline(
        device,
        path,
        false,
        vertexEntryPointName,
        fragmentEntryPointName,
        pipelineDesc,
        outPipeline
    );
}

inline Result createRenderPipelineFromSource(
    IDevice* device,
    const char* path,
    const char* vertexEntryPointName,
    const char* fragmentEntryPointName,
    const RenderPipelineDesc& pipelineDesc,
    IRenderPipeline** outPipeline
)
{
    return _createRenderPipeline(
        device,
        path,
        true,
        vertexEntryPointName,
        fragmentEntryPointName,
        pipelineDesc,
        outPipeline
    );
}

// ---------------------------------------------------------------------------------------
// Threading
// ---------------------------------------------------------------------------------------

template<typename T>
struct ioterable
{
    using iterator_category = std::forward_iterator_tag;
    using value_type = T;
    using difference_type = T;
    using pointer = std::add_pointer_t<T>;
    using reference = T;

    explicit ioterable(T n)
        : val_(n)
    {
    }

    ioterable() = default;
    ioterable(ioterable&&) = default;
    ioterable(const ioterable&) = default;
    ioterable& operator=(ioterable&&) = default;
    ioterable& operator=(const ioterable&) = default;

    ioterable& operator++()
    {
        ++val_;
        return *this;
    }
    ioterable operator++(int)
    {
        ioterable tmp(*this);
        ++val_;
        return tmp;
    }
    bool operator==(const ioterable& other) const { return val_ == other.val_; }
    bool operator!=(const ioterable& other) const { return val_ != other.val_; }

    value_type operator*() const { return val_; }

private:
    T val_{std::numeric_limits<T>::max()};
};

template<typename T, typename Func>
void parallelFor(T begin, T end, Func&& func)
{
    std::for_each(std::execution::par, ioterable(begin), ioterable(end), [&](T i) { func(i); });
}

template<typename T, typename Func>
void parallelForEach(std::vector<T>& vec, Func&& func)
{
    parallelFor(size_t(0), vec.size(), [&](size_t i) { func(vec[i]); });
}
