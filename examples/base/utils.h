#pragma once

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <execution>
#include <limits>
#include <vector>

// ---------------------------------------------------------------------------------------
// Asserts
// ---------------------------------------------------------------------------------------

#define ASSERT(cond, msg)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            fprintf(stderr, "Assertion failed: %s (%s:%d): %s\n", #cond, __FILE__, __LINE__, msg);                     \
            abort();                                                                                                   \
        }                                                                                                              \
    }                                                                                                                  \
    while (0)

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

namespace rhi {

// ---------------------------------------------------------------------------------------
// Debug printer
// ---------------------------------------------------------------------------------------

// A simple implementation of IDebugCallback that prints messages to stdout.
class DebugPrinter : public IDebugCallback
{
public:
    virtual SLANG_NO_THROW void SLANG_MCALL handleMessage(
        DebugMessageType type,
        DebugMessageSource source,
        const char* message
    ) override
    {
        static const char* kTypeStrings[] = {"INFO", "WARN", "ERROR"};
        static const char* kSourceStrings[] = {"Layer", "Driver", "Slang"};
        printf("[%s] (%s) %s\n", kTypeStrings[int(type)], kSourceStrings[int(source)], message);
        fflush(stdout);
    }

    static DebugPrinter* getInstance()
    {
        static DebugPrinter instance;
        return &instance;
    }
};

// ---------------------------------------------------------------------------------------
// Device creation helper
// ---------------------------------------------------------------------------------------

inline Result createDevice(
    DeviceType deviceType,
    std::vector<Feature> requiredFeatures,
    std::vector<std::pair<std::string, std::string>> preprocessorMacros,
    IDevice** outDevice
)
{
    DeviceDesc deviceDesc = {};
    deviceDesc.deviceType = deviceType;
#if SLANG_RHI_DEBUG
    getRHI()->enableDebugLayers();
    deviceDesc.enableValidation = true;
    deviceDesc.debugCallback = DebugPrinter::getInstance();
#endif
    const char* searchPaths[] = {EXAMPLE_DIR};
    deviceDesc.slang.searchPaths = searchPaths;
    deviceDesc.slang.searchPathCount = SLANG_COUNT_OF(searchPaths);

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

    SLANG_RETURN_ON_FAIL(getRHI()->createDevice(deviceDesc, outDevice));

    for (const auto& feature : requiredFeatures)
    {
        if (!(*outDevice)->hasFeature(feature))
        {
            return SLANG_E_NOT_AVAILABLE;
        }
    }

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
            printf("%s\n", msg);                                                                                       \
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
        printf("Failed to load Slang module from '%s'\n", pathOrSource);
        return SLANG_FAIL;
    }
    std::vector<slang::IComponentType*> entryPoints;
    for (const char* entryPointName : entryPointNames)
    {
        slang::IEntryPoint* entryPoint;
        if (!SLANG_SUCCEEDED(module->findEntryPointByName(entryPointName, &entryPoint)))
        {
            printf("Failed to find entry point '%s' in module '%s'\n", entryPointName, pathOrSource);
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

} // namespace rhi

// ---------------------------------------------------------------------------------------
// Threading
// ---------------------------------------------------------------------------------------

// TODO: Not available on macOS
#if 0
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
    std::for_each(
        std::execution::par,
        ioterable(begin),
        ioterable(end),
        [&](T i)
        {
            func(i);
        }
    );
}

template<typename T, typename Func>
void parallelForEach(std::vector<T>& vec, Func&& func)
{
    parallelFor(
        size_t(0),
        vec.size(),
        [&](size_t i)
        {
            func(vec[i]);
        }
    );
}
#endif
