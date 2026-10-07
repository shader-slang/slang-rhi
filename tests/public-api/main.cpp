// This test must build using only installed public headers and libraries.
#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <cstdio>
#include <cstring>

using namespace rhi;

#define CHECK_TEST(expression)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #expression);                               \
            return false;                                                                                              \
        }                                                                                                              \
    }                                                                                                                  \
    while (false)

static void printDiagnostics(ISlangBlob* diagnostics)
{
    if (diagnostics && diagnostics->getBufferSize())
        std::fprintf(stderr, "%.*s\n", int(diagnostics->getBufferSize()), (const char*)diagnostics->getBufferPointer());
}

static bool testGlobals()
{
    CHECK_TEST(getRHI() != nullptr);
    CHECK_TEST(getRHI() == rhiGetInstance());
    CHECK_TEST(SLANG_SUCCEEDED(getRHI()->initTaskPool(2)));
    CHECK_TEST(getFormatInfo(Format::R32Float).blockSizeInBytes == sizeof(float));

    const char data[] = "Public API across a library boundary";
    auto blob = getRHI()->createBlob(data, sizeof(data));
    CHECK_TEST(blob != nullptr);
    CHECK_TEST(blob->getBufferSize() == sizeof(data));
    CHECK_TEST(std::memcmp(blob->getBufferPointer(), data, sizeof(data)) == 0);
    // Exercise AddRef/Release from the consumer as well as creation in the library.
    ComPtr<ISlangBlob> retained = blob;
    blob.setNull();
    CHECK_TEST(std::memcmp(retained->getBufferPointer(), data, sizeof(data)) == 0);
    return true;
}

static bool testDevice(DeviceType type)
{
    std::printf("Testing %s\n", getRHI()->getDeviceTypeName(type));
    CHECK_TEST(getRHI()->isDeviceTypeSupported(type));
    DeviceDesc desc = {};
    desc.deviceType = type;
    desc.enableValidation = true;
    auto device = getRHI()->createDevice(desc);
    CHECK_TEST(device != nullptr);
    CHECK_TEST(device->getDeviceType() == type);

    auto session = device->getSlangSession();
    CHECK_TEST(session != nullptr);
    const char* source = R"(
        RWStructuredBuffer<float> buffer;
        uniform float value;
        [shader("compute")]
        [numthreads(4, 1, 1)]
        void computeMain(uint3 tid : SV_DispatchThreadID)
        {
            buffer[tid.x] += value;
        }
    )";
    ComPtr<ISlangBlob> diagnostics;
    auto module = session->loadModuleFromSourceString("public-api", "public-api.slang", source, diagnostics.writeRef());
    printDiagnostics(diagnostics);
    CHECK_TEST(module != nullptr);
    ComPtr<slang::IEntryPoint> entryPoint;
    CHECK_TEST(SLANG_SUCCEEDED(module->findEntryPointByName("computeMain", entryPoint.writeRef())));
    slang::IComponentType* entryPoints[] = {entryPoint.get()};
    ShaderProgramDesc programDesc = {};
    programDesc.slangGlobalScope = module;
    programDesc.slangEntryPoints = entryPoints;
    programDesc.slangEntryPointCount = 1;
    ComPtr<IShaderProgram> program;
    Result result = device->createShaderProgram(programDesc, program.writeRef(), diagnostics.writeRef());
    printDiagnostics(diagnostics);
    CHECK_TEST(SLANG_SUCCEEDED(result));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program;
    auto pipeline = device->createComputePipeline(pipelineDesc);
    CHECK_TEST(pipeline != nullptr);

    const float initial[] = {0.f, 1.f, 2.f, 3.f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(initial);
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource |
                       BufferUsage::CopyDestination;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    auto buffer = device->createBuffer(bufferDesc, initial);
    CHECK_TEST(buffer != nullptr);

    auto queue = device->getQueue(QueueType::Graphics);
    CHECK_TEST(queue != nullptr);
    auto encoder = queue->createCommandEncoder();
    CHECK_TEST(encoder != nullptr);
    auto pass = encoder->beginComputePass();
    CHECK_TEST(pass != nullptr);
    auto root = pass->bindPipeline(pipeline);
    CHECK_TEST(root != nullptr);
    ShaderCursor cursor(root);
    CHECK_TEST(SLANG_SUCCEEDED(cursor["buffer"].setBinding(buffer)));
    CHECK_TEST(SLANG_SUCCEEDED(cursor["value"].setData(10.f)));
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    auto commands = encoder->finish();
    CHECK_TEST(commands != nullptr);
    CHECK_TEST(SLANG_SUCCEEDED(queue->submit(commands)));
    CHECK_TEST(SLANG_SUCCEEDED(queue->waitOnHost()));

    float actual[4] = {};
    CHECK_TEST(SLANG_SUCCEEDED(device->readBuffer(buffer, 0, sizeof(actual), actual)));
    for (int i = 0; i < 4; ++i)
        CHECK_TEST(actual[i] == initial[i] + 10.f);
    return true;
}

static bool runTests(int argc, char** argv)
{
    CHECK_TEST(testGlobals());
    if (argc == 1)
    {
#if SLANG_RHI_ENABLE_CPU
        CHECK_TEST(testDevice(DeviceType::CPU));
#endif
    }
    for (int i = 1; i < argc; ++i)
    {
        struct Backend
        {
            const char* name;
            DeviceType type;
        };
        const Backend backends[] = {
            {"cpu", DeviceType::CPU},
            {"d3d11", DeviceType::D3D11},
            {"d3d12", DeviceType::D3D12},
            {"vulkan", DeviceType::Vulkan},
            {"metal", DeviceType::Metal},
            {"cuda", DeviceType::CUDA},
            {"wgpu", DeviceType::WGPU},
        };
        bool found = false;
        for (const auto& backend : backends)
        {
            if (std::strcmp(argv[i], backend.name) == 0)
            {
                CHECK_TEST(testDevice(backend.type));
                found = true;
                break;
            }
        }
        CHECK_TEST(found);
    }
    CHECK_TEST(SLANG_SUCCEEDED(destroyRHI()));
    // Check initialization again after complete teardown.
    CHECK_TEST(testGlobals());
    CHECK_TEST(SLANG_SUCCEEDED(destroyRHI()));
    return true;
}

int main(int argc, char** argv)
{
    return runTests(argc, argv) ? 0 : 1;
}
