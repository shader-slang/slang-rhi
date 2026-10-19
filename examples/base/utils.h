#pragma once

#include "math.h"
#include "../../src/enum-strings.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <execution>
#include <limits>
#include <mutex>
#include <random>
#include <span>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------------------

namespace rhi::detail {

inline void logMessage(DebugMessageType type, const char* format, ...)
{
    // Driver callbacks may log concurrently with the example.
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    FILE* stream = type == DebugMessageType::Info ? stdout : stderr;
    std::fprintf(stream, "[%s] ", enumToString(type));
    va_list args;
    va_start(args, format);
    std::vfprintf(stream, format, args);
    va_end(args);
    std::fputc('\n', stream);
    std::fflush(stream);
}

} // namespace rhi::detail

// printf-style formatting; the logger adds the level prefix and trailing newline.
#define LOG_INFO(...) ::rhi::detail::logMessage(::rhi::DebugMessageType::Info, __VA_ARGS__)
#define LOG_WARNING(...) ::rhi::detail::logMessage(::rhi::DebugMessageType::Warning, __VA_ARGS__)
#define LOG_ERROR(...) ::rhi::detail::logMessage(::rhi::DebugMessageType::Error, __VA_ARGS__)

// ---------------------------------------------------------------------------------------
// Asserts
// ---------------------------------------------------------------------------------------

#define ASSERT(cond, msg)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            LOG_ERROR("Assertion failed: %s (%s:%d): %s", #cond, __FILE__, __LINE__, msg);                             \
            std::abort();                                                                                              \
        }                                                                                                              \
    }                                                                                                                  \
    while (0)

namespace rhi {

// ---------------------------------------------------------------------------------------
// Random number generator
// ---------------------------------------------------------------------------------------

// Seeded scene generation, identical across backend windows.
struct RNG
{
    std::mt19937 generator;

    explicit RNG(uint32_t seed = 123456789)
        : generator(seed)
    {
    }

    uint32_t nextU32() { return generator(); }
    float nextFloat() { return float(nextU32() >> 8) * 0x1p-24f; }
    math::float3 nextFloat3()
    {
        // Sequence the draws explicitly; function argument evaluation order varies by compiler.
        float x = nextFloat();
        float y = nextFloat();
        float z = nextFloat();
        return {x, y, z};
    }
};

// ---------------------------------------------------------------------------------------
// Buffer upload helper
// ---------------------------------------------------------------------------------------

template<typename T>
Result uploadBuffer(IDevice* device, std::span<const T> data, BufferUsage usage, const char* label, IBuffer** outBuffer)
{
    BufferDesc desc = {};
    desc.size = data.size_bytes();
    desc.usage = usage;
    desc.label = label;
    return device->createBuffer(desc, data.data(), outBuffer);
}

// ---------------------------------------------------------------------------------------
// Debug printer
// ---------------------------------------------------------------------------------------

// Forward RHI and driver messages through the example logger.
class DebugPrinter : public IDebugCallback
{
public:
    explicit DebugPrinter(DebugMessageType minimumType)
        : m_minimumType(minimumType)
    {
    }

    virtual SLANG_NO_THROW void SLANG_MCALL handleMessage(
        DebugMessageType type,
        DebugMessageSource source,
        const char* message
    ) override
    {
        if (type < m_minimumType)
            return;
        detail::logMessage(type, "(%s) %s", enumToString(source), message);
    }

private:
    DebugMessageType m_minimumType;
};

// ---------------------------------------------------------------------------------------
// Device creation helper
// ---------------------------------------------------------------------------------------

inline Result createDevice(
    DeviceType deviceType,
    std::vector<Feature> requiredFeatures,
    const char* shaderSearchPath,
    std::vector<std::pair<std::string, std::string>> preprocessorMacros,
    IDevice** outDevice,
    std::vector<Feature>* outMissingFeatures = nullptr
)
{
    *outDevice = nullptr;
    if (outMissingFeatures)
        outMissingFeatures->clear();

    DeviceDesc deviceDesc = {};
    deviceDesc.deviceType = deviceType;
#if SLANG_RHI_DEBUG
    deviceDesc.enableValidation = true;
    static DebugPrinter debugPrinter(DebugMessageType::Warning);
    deviceDesc.debugCallback = &debugPrinter;
#endif
    const char* searchPaths[] = {shaderSearchPath};
    deviceDesc.slang.searchPaths = searchPaths;
    deviceDesc.slang.searchPathCount = SLANG_COUNT_OF(searchPaths);

    std::vector<slang::CompilerOptionEntry> compilerOptions;

#if SLANG_RHI_ENABLE_NVAPI
    // Setup NVAPI shader extension
    if (deviceType == DeviceType::D3D12)
    {
        deviceDesc.nvapiExtUavSlot = 999;
        preprocessorMacros.push_back({"NV_SHADER_EXTN_SLOT", "u999"});
        slang::CompilerOptionEntry nvapiSearchPath = {};
        nvapiSearchPath.name = slang::CompilerOptionName::DownstreamArgs;
        nvapiSearchPath.value.kind = slang::CompilerOptionValueKind::String;
        nvapiSearchPath.value.stringValue0 = "dxc";
        nvapiSearchPath.value.stringValue1 = "-I" SLANG_RHI_NVAPI_INCLUDE_DIR;
        compilerOptions.push_back(nvapiSearchPath);
    }
#endif

#if SLANG_RHI_ENABLE_OPTIX
    // Let the CUDA backend select headers matching the initialized OptiX runtime.
    if (deviceType == DeviceType::CUDA)
    {
        deviceDesc.optixDeviceHeaderRoot = SLANG_RHI_OPTIX_DEVICE_HEADER_INCLUDE_DIR;
    }
#endif

    std::vector<slang::PreprocessorMacroDesc> preprocessorMacrosDescs;
    for (const auto& macro : preprocessorMacros)
    {
        slang::PreprocessorMacroDesc desc;
        desc.name = macro.first.c_str();
        desc.value = macro.second.c_str();
        preprocessorMacrosDescs.push_back(desc);
    }

    deviceDesc.slang.preprocessorMacros = preprocessorMacrosDescs.data();
    deviceDesc.slang.preprocessorMacroCount = preprocessorMacrosDescs.size();

    deviceDesc.slang.compilerOptionEntries = compilerOptions.data();
    deviceDesc.slang.compilerOptionEntryCount = (uint32_t)compilerOptions.size();

    // Check example requirements after creation so an unsupported example is
    // reported once, rather than as a separate device error for each feature.
    ComPtr<IDevice> device;
    SLANG_RETURN_ON_FAIL(getRHI()->createDevice(deviceDesc, device.writeRef()));
    bool hasMissingFeatures = false;
    for (Feature feature : requiredFeatures)
    {
        if (!device->hasFeature(feature))
        {
            hasMissingFeatures = true;
            if (outMissingFeatures)
                outMissingFeatures->push_back(feature);
        }
    }
    if (hasMissingFeatures)
        return SLANG_E_NOT_AVAILABLE;

    *outDevice = device.detach();
    return SLANG_OK;
}

// ---------------------------------------------------------------------------------------
// Program and pipeline creation helpers
// ---------------------------------------------------------------------------------------

#define PRINT_DIAGNOSTICS(diagnostics)                                                                                 \
    {                                                                                                                  \
        if (diagnostics)                                                                                               \
        {                                                                                                              \
            const char* msg = (const char*)diagnostics->getBufferPointer();                                            \
            LOG_INFO("%s", msg);                                                                                       \
        }                                                                                                              \
    }

inline Result _createProgram(
    IDevice* device,
    const char* pathOrSource,
    bool isSource,
    const std::vector<const char*>& entryPointNames,
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
        LOG_ERROR("Failed to load Slang module from '%s'", pathOrSource);
        return SLANG_FAIL;
    }
    std::vector<slang::IComponentType*> entryPoints;
    for (const char* entryPointName : entryPointNames)
    {
        slang::IEntryPoint* entryPoint;
        if (!SLANG_SUCCEEDED(module->findEntryPointByName(entryPointName, &entryPoint)))
        {
            LOG_ERROR("Failed to find entry point '%s' in module '%s'", entryPointName, pathOrSource);
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
        LOG_ERROR("Failed to create program for module '%s'", pathOrSource);
        return SLANG_FAIL;
    }
    return SLANG_OK;
}

inline Result createProgram(
    IDevice* device,
    const char* path,
    const std::vector<const char*>& entryPointNames,
    IShaderProgram** outProgram
)
{
    return _createProgram(device, path, false, entryPointNames, outProgram);
}

inline Result createProgramFromSource(
    IDevice* device,
    const char* source,
    const std::vector<const char*>& entryPointNames,
    IShaderProgram** outProgram
)
{
    return _createProgram(device, source, true, entryPointNames, outProgram);
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
    SLANG_RETURN_ON_FAIL(_createProgram(device, pathOrSource, isSource, {entryPointName}, program.writeRef()));

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
    SLANG_RETURN_ON_FAIL(_createProgram(
        device,
        pathOrSource,
        isSource,
        {vertexEntryPointName, fragmentEntryPointName},
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
// Blitter
// ---------------------------------------------------------------------------------------

class Blitter
{
public:
    Blitter(IDevice* device)
        : m_device(device)
    {
    }

    Result blit(ITexture* dst, ITexture* src, ICommandEncoder* commandEncoder)
    {
        if (!dst || !src || !commandEncoder)
        {
            return SLANG_E_INVALID_ARG;
        }

        const char* blitComputeShader = R"(
        #define DST_FORMAT "%s"
        #define DST_SRGB %s

        float linearToSrgb(float linear)
        {
            if (linear <= 0.0031308)
                return linear * 12.92;
            else
                return pow(linear, (1.0 / 2.4)) * (1.055) - 0.055;
        }

        vector<float, N> linearToSrgb<let N : int>(vector<float, N> linear)
        {
            vector<float, N> result;
            [ForceUnroll]
            for (int i = 0; i < N; ++i)
            {
                result[i] = linearToSrgb(linear[i]);
            }
            return result;
        }

        [shader("compute")]
        [numthreads(16, 16, 1)]
        void mainCompute(uint3 tid: SV_DispatchThreadID, [format(DST_FORMAT)] RWTexture2D<float4> dst, Texture2D<float4> src)
        {
            int2 size;
            src.GetDimensions(size.x, size.y);
            if (any(tid.xy >= size))
                return;
            float4 color = src[tid.xy];
            if (DST_SRGB)
                color = linearToSrgb<4>(color);
            dst[tid.xy] = color;
        }
    )";

        const char* blitRenderShader = R"(
        struct VSOut {
            float4 pos : SV_Position;
            float2 uv : UV;
        };

        [shader("vertex")]
        VSOut mainVertex(uint vid: SV_VertexID)
        {
            VSOut vsOut;
            vsOut.uv = float2((vid << 1) & 2, vid & 2);
            vsOut.pos = float4(vsOut.uv * float2(2, -2) + float2(-1, 1), 0, 1);
            return vsOut;
        }

        [shader("fragment")]
        float4 mainFragment(VSOut vsOut, Texture2D<float4> src) : SV_Target
        {
            float2 uv = vsOut.uv;
            int2 size;
            src.GetDimensions(size.x, size.y);
            int2 coord = int2(uv * size);
            return src[coord];
        }
    )";


        uint32_t width = src->getDesc().size.width;
        uint32_t height = src->getDesc().size.height;
        Format dstFormat = dst->getDesc().format;
        const FormatInfo& dstFormatInfo = getFormatInfo(dstFormat);

        if (is_set(dst->getDesc().usage, TextureUsage::RenderTarget))
        {
            if (!m_blitRenderPipeline)
            {
                RenderPipelineDesc pipelineDesc = {};
                ColorTargetDesc colorTarget = {};
                colorTarget.format = dstFormat;
                pipelineDesc.targets = &colorTarget;
                pipelineDesc.targetCount = 1;
                SLANG_RETURN_ON_FAIL(createRenderPipelineFromSource(
                    m_device,
                    blitRenderShader,
                    "mainVertex",
                    "mainFragment",
                    pipelineDesc,
                    m_blitRenderPipeline.writeRef()
                ));
            }
            RenderPassColorAttachment colorAttachment = {};
            colorAttachment.view = dst->getDefaultView();
            RenderPassDesc renderPassDesc = {};
            renderPassDesc.colorAttachments = &colorAttachment;
            renderPassDesc.colorAttachmentCount = 1;
            IRenderPassEncoder* passEncoder = commandEncoder->beginRenderPass(renderPassDesc);
            ShaderCursor cursor(passEncoder->bindPipeline(m_blitRenderPipeline));
            cursor["src"].setBinding(src);
            RenderState renderState = {};
            renderState.viewports[0] = Viewport::fromSize(width, height);
            renderState.viewportCount = 1;
            renderState.scissorRects[0] = ScissorRect::fromSize(width, height);
            renderState.scissorRectCount = 1;
            passEncoder->setRenderState(renderState);
            DrawArguments drawArgs = {};
            drawArgs.vertexCount = 3;
            passEncoder->draw(drawArgs);
            passEncoder->end();
        }
        else if (is_set(dst->getDesc().usage, TextureUsage::UnorderedAccess))
        {
            if (!m_blitComputePipeline)
            {
                const char* dstFormatAttribute = dstFormatInfo.slangName;
                if (dstFormat == Format::RGBA8UnormSrgb || dstFormat == Format::BGRA8UnormSrgb ||
                    dstFormat == Format::BGRX8UnormSrgb)
                {
                    dstFormatAttribute = "rgba8";
                }
                if (!dstFormatAttribute)
                {
                    return SLANG_E_INVALID_ARG;
                }
                char shader[4096];
                snprintf(
                    shader,
                    sizeof(shader),
                    blitComputeShader,
                    dstFormatAttribute,
                    dstFormatInfo.isSrgb ? "true" : "false"
                );

                SLANG_RETURN_ON_FAIL(
                    createComputePipelineFromSource(m_device, shader, "mainCompute", m_blitComputePipeline.writeRef())
                );
            }
            IComputePassEncoder* passEncoder = commandEncoder->beginComputePass();
            ShaderCursor cursor(passEncoder->bindPipeline(m_blitComputePipeline));
            cursor["dst"].setBinding(dst);
            cursor["src"].setBinding(src);
            passEncoder->dispatchCompute((width + 15) / 16, (height + 15) / 16, 1);
            passEncoder->end();
        }

        return SLANG_OK;
    }

private:
    IDevice* m_device;
    ComPtr<IComputePipeline> m_blitComputePipeline;
    ComPtr<IRenderPipeline> m_blitRenderPipeline;
};

// ---------------------------------------------------------------------------------------
// FrameStats
// ---------------------------------------------------------------------------------------

// Application-loop timing, shared in meaning across the side-by-side windows.
// A rolling window of 120 intervals smooths the display; publish at most 4 Hz.
class FrameStats
{
public:
    bool update(double time)
    {
        if (m_previousTime < 0 || time <= m_previousTime)
        {
            reset();
            m_previousTime = m_lastReport = time;
            return false;
        }
        double elapsed = time - m_previousTime;
        m_previousTime = time;
        m_total += elapsed - m_intervals[m_next];
        m_intervals[m_next] = elapsed;
        m_next = (m_next + 1) % m_intervals.size();
        if (m_count < m_intervals.size())
            ++m_count;
        if (time - m_lastReport < 0.25)
            return false;
        m_lastReport = time;
        m_frameTime = m_total / double(m_count);
        return true;
    }

    void reset() { *this = FrameStats(); }
    double fps() const { return m_frameTime > 0 ? 1.0 / m_frameTime : 0; }
    double milliseconds() const { return m_frameTime * 1000.0; }

private:
    std::array<double, 120> m_intervals = {};
    size_t m_next = 0, m_count = 0;
    double m_previousTime = -1, m_lastReport = 0, m_total = 0, m_frameTime = 0;
};

// ---------------------------------------------------------------------------------------
// FixedStepClock
// ---------------------------------------------------------------------------------------

// Every backend receives the same absolute application time. Never use draw
// duration as simulation time; clamp catch-up after a stall to keep input usable.
class FixedStepClock
{
public:
    explicit FixedStepClock(double step = 1.0 / 120.0, uint32_t maxCatchUpSteps = 8)
        : m_step(std::isfinite(step) && step > 0 ? step : 1.0 / 120.0)
        , m_maxCatchUpSteps(maxCatchUpSteps)
    {
    }
    double step() const { return m_step; }
    uint32_t update(double time, bool running)
    {
        double elapsed = m_lastTime < 0 ? 0 : std::clamp(time - m_lastTime, 0.0, m_maxCatchUpSteps * m_step);
        m_lastTime = time;
        if (!running)
        {
            m_accumulator = 0;
            return 0;
        }
        m_accumulator += elapsed;
        uint32_t steps = uint32_t(std::floor((m_accumulator + 1e-10) / m_step));
        m_accumulator = std::max(0.0, m_accumulator - steps * m_step);
        return steps;
    }
    void reset() { m_accumulator = 0; }

private:
    double m_step;
    uint32_t m_maxCatchUpSteps;
    double m_lastTime = -1, m_accumulator = 0;
};

} // namespace rhi
