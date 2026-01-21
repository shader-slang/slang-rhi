#include "testing.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <algorithm>

using namespace rhi;
using namespace rhi::testing;

class VirtualShaderCache : public IPersistentCache
{
public:
    struct Stats
    {
        uint32_t missCount = 0;
        uint32_t hitCount = 0;
        uint32_t entryCount = 0;
    };

    using Key = std::vector<uint8_t>;
    using Data = std::vector<uint8_t>;

    struct Entry
    {
        uint32_t ticket;
        std::vector<uint8_t> data;
    };

    std::map<Key, Entry> entries;
    Stats stats;
    uint32_t maxEntryCount = 1024;
    uint32_t ticketCounter = 0;

    void clear()
    {
        entries.clear();
        stats = {};
        maxEntryCount = 1024;
        ticketCounter = 0;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL writeCache(ISlangBlob* key_, ISlangBlob* data_) override
    {
        while (entries.size() >= maxEntryCount)
        {
            auto it = std::min_element(
                entries.begin(),
                entries.end(),
                [](const auto& a, const auto& b)
                {
                    return a.second.ticket < b.second.ticket;
                }
            );
            entries.erase(it);
        }

        Key key(
            static_cast<const uint8_t*>(key_->getBufferPointer()),
            static_cast<const uint8_t*>(key_->getBufferPointer()) + key_->getBufferSize()
        );
        Data data(
            static_cast<const uint8_t*>(data_->getBufferPointer()),
            static_cast<const uint8_t*>(data_->getBufferPointer()) + data_->getBufferSize()
        );
        entries[key] = {ticketCounter++, data};
        stats.entryCount = entries.size();
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL queryCache(ISlangBlob* key_, ISlangBlob** outData) override
    {
        Key key(
            static_cast<const uint8_t*>(key_->getBufferPointer()),
            static_cast<const uint8_t*>(key_->getBufferPointer()) + key_->getBufferSize()
        );
        auto it = entries.find(key);
        if (it == entries.end())
        {
            stats.missCount++;
            *outData = nullptr;
            return SLANG_E_NOT_FOUND;
        }
        stats.hitCount++;
        *outData = UnownedBlob::create(it->second.data.data(), it->second.data.size()).detach();
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(const SlangUUID& uuid, void** outObject) override
    {
        if (uuid == IPersistentCache::getTypeGuid())
        {
            *outObject = static_cast<IPersistentCache*>(this);
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
    }

    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override
    {
        // The lifetime of this object is tied to the test.
        // Do not perform any reference counting.
        return 2;
    }

    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override
    {
        // Returning 2 is important here, because when releasing a COM pointer, it checks
        // if the ref count **was 1 before releasing** in order to free the object.
        return 2;
    }
};

static VirtualShaderCache shaderCache;

// Base class for shader cache tests.
// Slang currently does not allow reloading shaders from modified sources.
// Because of this, the tests recreate a device for each test step,
// allowing to modify shader sources in between.
struct ShaderCacheTest
{
    GpuTestContext* ctx;
    std::filesystem::path tempDirectory;

    ComPtr<IDevice> device;
    ComPtr<IComputePipeline> computePipeline;
    ComPtr<IRenderPipeline> renderPipeline;
    ComPtr<IBuffer> rwBuffer;

    std::string computeShaderA = std::string(
        R"(
        [shader("compute")]
        [numthreads(4, 1, 1)]
        void main(
            uint3 sv_dispatchThreadID : SV_DispatchThreadID,
            uniform RWStructuredBuffer<float> buffer)
        {
            var input = buffer[sv_dispatchThreadID.x];
            buffer[sv_dispatchThreadID.x] = input + 1.0f;
        }
        )"
    );

    std::string computeShaderB = std::string(
        R"(
        [shader("compute")]
        [numthreads(4, 1, 1)]
        void main(
            uint3 sv_dispatchThreadID : SV_DispatchThreadID,
            uniform RWStructuredBuffer<float> buffer)
        {
            var input = buffer[sv_dispatchThreadID.x];
            buffer[sv_dispatchThreadID.x] = input + 2.0f;
        }
        )"
    );

    std::string computeShaderC = std::string(
        R"(
        [shader("compute")]
        [numthreads(4, 1, 1)]
        void main(
            uint3 sv_dispatchThreadID : SV_DispatchThreadID,
            uniform RWStructuredBuffer<float> buffer)
        {
            var input = buffer[sv_dispatchThreadID.x];
            buffer[sv_dispatchThreadID.x] = input + 3.0f;
        }
        )"
    );

    void writeShader(const std::string& source, const std::string& fileName)
    {
        std::filesystem::path path = tempDirectory / fileName;
        std::ofstream ofs(path);
        REQUIRE(ofs.good());
        ofs.write(source.data(), source.size());
        ofs.close();
    }

    void createDevice()
    {
        DeviceDesc deviceDesc = {};
        deviceDesc.deviceType = ctx->deviceType;
        deviceDesc.adapter = getSelectedDeviceAdapter(ctx->deviceType);
        deviceDesc.slang.slangGlobalSession = ctx->slangGlobalSession;
        auto searchPaths = getSlangSearchPaths();
        std::string tempDirectoryStr = tempDirectory.string();
        searchPaths.push_back(tempDirectoryStr.c_str());
        deviceDesc.slang.searchPaths = searchPaths.data();
        deviceDesc.slang.searchPathCount = searchPaths.size();
        deviceDesc.persistentShaderCache = &shaderCache;

        std::vector<slang::CompilerOptionEntry> entries;
        slang::CompilerOptionEntry emitSpirvDirectlyEntry;
        emitSpirvDirectlyEntry.name = slang::CompilerOptionName::EmitSpirvDirectly;
        emitSpirvDirectlyEntry.value.intValue0 = 1;
        entries.push_back(emitSpirvDirectlyEntry);
        deviceDesc.slang.compilerOptionEntries = entries.data();
        deviceDesc.slang.compilerOptionEntryCount = entries.size();

        // TODO: We should also set the debug callback
        // (And in general reduce the differences (and duplication) between
        // here and render-test-main.cpp)
#if SLANG_RHI_DEBUG
        deviceDesc.enableValidation = true;
#endif

        REQUIRE_CALL(getRHI()->createDevice(deviceDesc, device.writeRef()));
    }

    void createComputeResources()
    {
        const int numberCount = 4;
        float initialData[] = {0.0f, 1.0f, 2.0f, 3.0f};
        BufferDesc bufferDesc = {};
        bufferDesc.size = numberCount * sizeof(float);
        bufferDesc.format = Format::Undefined;
        bufferDesc.elementSize = sizeof(float);
        bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                           BufferUsage::CopySource;
        bufferDesc.defaultState = ResourceState::UnorderedAccess;
        bufferDesc.memoryType = MemoryType::DeviceLocal;

        REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)initialData, rwBuffer.writeRef()));
    }

    void freeComputeResources()
    {
        rwBuffer = nullptr;
        computePipeline = nullptr;
    }

    void createComputePipeline(const char* moduleName, const char* entryPointName)
    {
        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(loadAndLinkProgram(device, moduleName, entryPointName, shaderProgram.writeRef()));

        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, computePipeline.writeRef()));
    }

    void createComputePipeline(std::string shaderSource)
    {
        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(loadComputeProgramFromSource(device, shaderSource, shaderProgram.writeRef()));

        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, computePipeline.writeRef()));
    }

    void dispatchComputePipeline()
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(computePipeline);
        ShaderCursor entryPointCursor(rootObject->getEntryPoint(0));
        entryPointCursor["buffer"].setBinding(rwBuffer);
        passEncoder->dispatchCompute(4, 1, 1);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    bool checkOutput(const std::vector<float>& expectedOutput)
    {
        ComPtr<ISlangBlob> bufferBlob;
        device->readBuffer(rwBuffer, 0, 4 * sizeof(float), bufferBlob.writeRef());
        REQUIRE(bufferBlob);
        REQUIRE(bufferBlob->getBufferSize() == expectedOutput.size() * sizeof(float));
        return ::memcmp(bufferBlob->getBufferPointer(), expectedOutput.data(), bufferBlob->getBufferSize()) == 0;
    }

    void runComputePipeline(
        const char* moduleName,
        const char* entryPointName,
        const std::vector<float>& expectedOutput
    )
    {
        createComputeResources();
        createComputePipeline(moduleName, entryPointName);
        dispatchComputePipeline();
        CHECK(checkOutput(expectedOutput));
        freeComputeResources();
    }

    void runComputePipeline(std::string shaderSource, const std::vector<float>& expectedOutput)
    {
        createComputeResources();
        createComputePipeline(shaderSource);
        dispatchComputePipeline();
        CHECK(checkOutput(expectedOutput));
        freeComputeResources();
    }

    VirtualShaderCache::Stats getStats() { return shaderCache.stats; }

    void run(GpuTestContext* ctx_, std::string tempDirectory_)
    {
        ctx = ctx_;
        tempDirectory = tempDirectory_;

        shaderCache.clear();
        remove_all(tempDirectory);
        create_directories(tempDirectory);

        runTests();

        remove_all(tempDirectory);
    }

    virtual void runTests() = 0;
};

// Basic shader cache test using 3 different shader files stored on disk.
struct ShaderCacheTestSourceFile : ShaderCacheTest
{
    void runTests()
    {
        // Write shader source files.
        writeShader(computeShaderA, "shader-cache-tmp-a.slang");
        writeShader(computeShaderB, "shader-cache-tmp-b.slang");
        writeShader(computeShaderC, "shader-cache-tmp-c.slang");

        // Cache is cold and we expect 3 misses.
        createDevice();
        runComputePipeline("shader-cache-tmp-a", "main", {1.f, 2.f, 3.f, 4.f});
        runComputePipeline("shader-cache-tmp-b", "main", {2.f, 3.f, 4.f, 5.f});
        runComputePipeline("shader-cache-tmp-c", "main", {3.f, 4.f, 5.f, 6.f});
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 3);

        // Cache is hot and we expect 3 hits.
        createDevice();
        runComputePipeline("shader-cache-tmp-a", "main", {1.f, 2.f, 3.f, 4.f});
        runComputePipeline("shader-cache-tmp-b", "main", {2.f, 3.f, 4.f, 5.f});
        runComputePipeline("shader-cache-tmp-c", "main", {3.f, 4.f, 5.f, 6.f});
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 3);
        CHECK_EQ(getStats().entryCount, 3);

        // Write shader source files, all rotated by one.
        writeShader(computeShaderA, "shader-cache-tmp-b.slang");
        writeShader(computeShaderB, "shader-cache-tmp-c.slang");
        writeShader(computeShaderC, "shader-cache-tmp-a.slang");

        // Cache is cold again and we expect 3 misses.
        createDevice();
        runComputePipeline("shader-cache-tmp-b", "main", {1.f, 2.f, 3.f, 4.f});
        runComputePipeline("shader-cache-tmp-c", "main", {2.f, 3.f, 4.f, 5.f});
        runComputePipeline("shader-cache-tmp-a", "main", {3.f, 4.f, 5.f, 6.f});
        CHECK_EQ(getStats().missCount, 6);
        CHECK_EQ(getStats().hitCount, 3);
        CHECK_EQ(getStats().entryCount, 6);

        // Cache is hot again and we expect 3 hits.
        createDevice();
        runComputePipeline("shader-cache-tmp-b", "main", {1.f, 2.f, 3.f, 4.f});
        runComputePipeline("shader-cache-tmp-c", "main", {2.f, 3.f, 4.f, 5.f});
        runComputePipeline("shader-cache-tmp-a", "main", {3.f, 4.f, 5.f, 6.f});
        CHECK_EQ(getStats().missCount, 6);
        CHECK_EQ(getStats().hitCount, 6);
        CHECK_EQ(getStats().entryCount, 6);
    }
};

// Test caching of shaders that are compiled from source strings instead of files.
struct ShaderCacheTestSourceString : ShaderCacheTest
{
    void runTests()
    {
        // Cache is cold and we expect 3 misses.
        createDevice();
        runComputePipeline(computeShaderA, {1.f, 2.f, 3.f, 4.f});
        runComputePipeline(computeShaderB, {2.f, 3.f, 4.f, 5.f});
        runComputePipeline(computeShaderC, {3.f, 4.f, 5.f, 6.f});
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 3);

        // Cache is hot and we expect 3 hits.
        createDevice();
        runComputePipeline(computeShaderA, {1.f, 2.f, 3.f, 4.f});
        runComputePipeline(computeShaderB, {2.f, 3.f, 4.f, 5.f});
        runComputePipeline(computeShaderC, {3.f, 4.f, 5.f, 6.f});
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 3);
        CHECK_EQ(getStats().entryCount, 3);
    }
};

// Test one shader file on disk with multiple entry points.
struct ShaderCacheTestEntryPoint : ShaderCacheTest
{
    void runTests()
    {
        // Cache is cold and we expect 3 misses, one for each entry point.
        createDevice();
        runComputePipeline("test-shader-cache-multiple-entry-points", "computeA", {1.f, 2.f, 3.f, 4.f});
        runComputePipeline("test-shader-cache-multiple-entry-points", "computeB", {2.f, 3.f, 4.f, 5.f});
        runComputePipeline("test-shader-cache-multiple-entry-points", "computeC", {3.f, 4.f, 5.f, 6.f});
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 3);

        // Cache is hot and we expect 3 hits.
        createDevice();
        runComputePipeline("test-shader-cache-multiple-entry-points", "computeA", {1.f, 2.f, 3.f, 4.f});
        runComputePipeline("test-shader-cache-multiple-entry-points", "computeB", {2.f, 3.f, 4.f, 5.f});
        runComputePipeline("test-shader-cache-multiple-entry-points", "computeC", {3.f, 4.f, 5.f, 6.f});
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 3);
        CHECK_EQ(getStats().entryCount, 3);
    }
};

// Test cache invalidation due to an import/include file being changed on disk.
struct ShaderCacheTestImportInclude : ShaderCacheTest
{
    std::string importedContentsA = std::string(
        R"(
        public void processElement(RWStructuredBuffer<float> buffer, uint index)
        {
            var input = buffer[index];
            buffer[index] = input + 1.0f;
        }
        )"
    );

    std::string importedContentsB = std::string(
        R"(
        public void processElement(RWStructuredBuffer<float> buffer, uint index)
        {
            var input = buffer[index];
            buffer[index] = input + 2.0f;
        }
        )"
    );

    std::string importFile = std::string(
        R"(
        import shader_cache_tmp_imported;

        [shader("compute")]
        [numthreads(4, 1, 1)]
        void main(
            uint3 sv_dispatchThreadID : SV_DispatchThreadID,
            uniform RWStructuredBuffer<float> buffer)
        {
            processElement(buffer, sv_dispatchThreadID.x);
        }
        )"
    );

    std::string includeFile = std::string(
        R"(
        #include "shader-cache-tmp-imported.slang"

        [shader("compute")]
        [numthreads(4, 1, 1)]
        void main(
            uint3 sv_dispatchThreadID : SV_DispatchThreadID,
            uniform RWStructuredBuffer<float> buffer)
        {
            processElement(buffer, sv_dispatchThreadID.x);
        })"
    );

    void runTests()
    {
        // Write shader source files.
        writeShader(importedContentsA, "shader-cache-tmp-imported.slang");
        writeShader(importFile, "shader-cache-tmp-import.slang");
        writeShader(includeFile, "shader-cache-tmp-include.slang");

        // Cache is cold and we expect 2 misses.
        createDevice();
        runComputePipeline("shader-cache-tmp-import", "main", {1.f, 2.f, 3.f, 4.f});
        runComputePipeline("shader-cache-tmp-include", "main", {1.f, 2.f, 3.f, 4.f});
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 2);

        // Cache is hot and we expect 2 hits.
        createDevice();
        runComputePipeline("shader-cache-tmp-import", "main", {1.f, 2.f, 3.f, 4.f});
        runComputePipeline("shader-cache-tmp-include", "main", {1.f, 2.f, 3.f, 4.f});
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 2);
        CHECK_EQ(getStats().entryCount, 2);

        // Change content of imported/included shader file.
        writeShader(importedContentsB, "shader-cache-tmp-imported.slang");

        // Cache is cold and we expect 2 misses.
        createDevice();
        runComputePipeline("shader-cache-tmp-import", "main", {2.f, 3.f, 4.f, 5.f});
        runComputePipeline("shader-cache-tmp-include", "main", {2.f, 3.f, 4.f, 5.f});
        CHECK_EQ(getStats().missCount, 4);
        CHECK_EQ(getStats().hitCount, 2);
        CHECK_EQ(getStats().entryCount, 4);

        // Cache is hot and we expect 2 hits.
        createDevice();
        runComputePipeline("shader-cache-tmp-import", "main", {2.f, 3.f, 4.f, 5.f});
        runComputePipeline("shader-cache-tmp-include", "main", {2.f, 3.f, 4.f, 5.f});
        CHECK_EQ(getStats().missCount, 4);
        CHECK_EQ(getStats().hitCount, 4);
        CHECK_EQ(getStats().entryCount, 4);
    }
};

// One shader featuring multiple kinds of shader objects that can be bound.
struct ShaderCacheTestSpecialization : ShaderCacheTest
{
    slang::ProgramLayout* slangReflection = nullptr;

    void createComputePipeline()
    {
        ComPtr<IShaderProgram> shaderProgram;

        REQUIRE_CALL(loadAndLinkProgram(
            device,
            "test-shader-cache-specialization",
            "computeMain",
            shaderProgram.writeRef(),
            &slangReflection
        ));

        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, computePipeline.writeRef()));
    }

    void dispatchComputePipeline(const char* transformerTypeName)
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(computePipeline);

        ComPtr<IShaderObject> transformer;
        slang::TypeReflection* transformerType = slangReflection->findTypeByName(transformerTypeName);
        REQUIRE_CALL(
            device
                ->createShaderObject(nullptr, transformerType, ShaderObjectContainerType::None, transformer.writeRef())
        );

        float c = 5.f;
        ShaderCursor(transformer)["c"].setData(&c, sizeof(float));
        transformer->finalize();

        ShaderCursor entryPointCursor(rootObject->getEntryPoint(0));
        entryPointCursor["buffer"].setBinding(rwBuffer);
        entryPointCursor["transformer"].setObject(transformer);

        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    void runComputePipeline(const char* transformerTypeName, const std::vector<float>& expectedOutput)
    {
        createComputeResources();
        createComputePipeline();
        dispatchComputePipeline(transformerTypeName);
        CHECK(checkOutput(expectedOutput));
        freeComputeResources();
    }

    void runTests()
    {
        // Cache is cold and we expect 2 misses.
        createDevice();
        runComputePipeline("AddTransformer", {5.f, 6.f, 7.f, 8.f});
        runComputePipeline("MulTransformer", {0.f, 5.f, 10.f, 15.f});
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 2);

        // Cache is hot and we expect 2 hits.
        createDevice();
        runComputePipeline("AddTransformer", {5.f, 6.f, 7.f, 8.f});
        runComputePipeline("MulTransformer", {0.f, 5.f, 10.f, 15.f});
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 2);
        CHECK_EQ(getStats().entryCount, 2);
    }
};

struct ShaderCacheTestEviction : ShaderCacheTest
{
    void runTests()
    {
        shaderCache.maxEntryCount = 2;

        // Load shader A & B. Cache is cold and we expect 2 misses.
        createDevice();
        runComputePipeline(computeShaderA, {1.f, 2.f, 3.f, 4.f});
        runComputePipeline(computeShaderB, {2.f, 3.f, 4.f, 5.f});
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 2);

        // Load shader A & B. Cache is hot and we expect 2 hits.
        createDevice();
        runComputePipeline(computeShaderA, {1.f, 2.f, 3.f, 4.f});
        runComputePipeline(computeShaderB, {2.f, 3.f, 4.f, 5.f});
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 2);
        CHECK_EQ(getStats().entryCount, 2);

        // Load shader C. Cache is cold and we expect 1 miss.
        // This will evict the least frequently used entry (shader A).
        // We expect 2 entries in the cache (shader B & C).
        createDevice();
        runComputePipeline(computeShaderC, {3.f, 4.f, 5.f, 6.f});
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 2);
        CHECK_EQ(getStats().entryCount, 2);

        // Load shader C. Cache is hot and we expect 1 hit.
        createDevice();
        runComputePipeline(computeShaderC, {3.f, 4.f, 5.f, 6.f});
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 3);
        CHECK_EQ(getStats().entryCount, 2);

        // Load shader B. Cache is hot and we expect 1 hit.
        createDevice();
        runComputePipeline(computeShaderB, {2.f, 3.f, 4.f, 5.f});
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 4);
        CHECK_EQ(getStats().entryCount, 2);

        // Load shader A. Cache is cold and we expect 1 miss.
        createDevice();
        runComputePipeline(computeShaderA, {1.f, 2.f, 3.f, 4.f});
        CHECK_EQ(getStats().missCount, 4);
        CHECK_EQ(getStats().hitCount, 4);
        CHECK_EQ(getStats().entryCount, 2);
    }
};

// Similar to ShaderCacheTestEntryPoint but with a source file containing a vertex and fragment shader.
struct ShaderCacheTestGraphics : ShaderCacheTest
{
    struct Vertex
    {
        float position[3];
    };

    static const int kWidth = 256;
    static const int kHeight = 256;
    static const Format format = Format::RGBA32Float;

    ComPtr<IBuffer> vertexBuffer;
    ComPtr<ITexture> colorBuffer;
    ComPtr<ITextureView> colorBufferView;
    ComPtr<IInputLayout> inputLayout;

    ComPtr<IBuffer> createVertexBuffer()
    {
        const Vertex vertices[] = {
            {0, 0, 0.5},
            {1, 0, 0.5},
            {0, 1, 0.5},
        };

        BufferDesc bufferDesc;
        bufferDesc.size = sizeof(vertices);
        bufferDesc.usage = BufferUsage::VertexBuffer;
        bufferDesc.defaultState = ResourceState::VertexBuffer;
        ComPtr<IBuffer> buffer = device->createBuffer(bufferDesc, vertices);
        REQUIRE(buffer != nullptr);
        return buffer;
    }

    ComPtr<ITexture> createColorBuffer()
    {
        TextureDesc textureDesc;
        textureDesc.type = TextureType::Texture2D;
        textureDesc.size.width = kWidth;
        textureDesc.size.height = kHeight;
        textureDesc.size.depth = 1;
        textureDesc.mipCount = 1;
        textureDesc.format = format;
        textureDesc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
        textureDesc.defaultState = ResourceState::RenderTarget;
        ComPtr<ITexture> texture = device->createTexture(textureDesc, nullptr);
        REQUIRE(texture != nullptr);
        return texture;
    }

    void createGraphicsResources()
    {
        VertexStreamDesc vertexStreams[] = {
            {sizeof(Vertex), InputSlotClass::PerVertex, 0},
        };

        InputElementDesc inputElements[] = {
            // Vertex buffer data
            {"POSITION", 0, Format::RGB32Float, offsetof(Vertex, position), 0},
        };
        InputLayoutDesc inputLayoutDesc = {};
        inputLayoutDesc.inputElementCount = SLANG_COUNT_OF(inputElements);
        inputLayoutDesc.inputElements = inputElements;
        inputLayoutDesc.vertexStreamCount = SLANG_COUNT_OF(vertexStreams);
        inputLayoutDesc.vertexStreams = vertexStreams;
        inputLayout = device->createInputLayout(inputLayoutDesc);
        REQUIRE(inputLayout != nullptr);

        vertexBuffer = createVertexBuffer();
        colorBuffer = createColorBuffer();

        TextureViewDesc colorBufferViewDesc = {};
        colorBufferViewDesc.format = format;
        REQUIRE_CALL(device->createTextureView(colorBuffer, colorBufferViewDesc, colorBufferView.writeRef()));
    }

    void freeGraphicsResources()
    {
        inputLayout = nullptr;
        vertexBuffer = nullptr;
        colorBuffer = nullptr;
        renderPipeline = nullptr;
        colorBufferView = nullptr;
    }

    void createGraphicsPipeline()
    {
        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(
            loadProgram(device, "test-shader-cache-graphics", {"vertexMain", "fragmentMain"}, shaderProgram.writeRef())
        );

        ColorTargetDesc target;
        target.format = format;
        RenderPipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        pipelineDesc.inputLayout = inputLayout;
        pipelineDesc.targets = &target;
        pipelineDesc.targetCount = 1;
        pipelineDesc.depthStencil.depthTestEnable = false;
        pipelineDesc.depthStencil.depthWriteEnable = false;
        REQUIRE_CALL(device->createRenderPipeline(pipelineDesc, renderPipeline.writeRef()));
    }

    void dispatchGraphicsPipeline()
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        RenderPassColorAttachment colorAttachment;
        colorAttachment.view = colorBufferView;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.storeOp = StoreOp::Store;
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;
        auto passEncoder = commandEncoder->beginRenderPass(renderPass);

        passEncoder->bindPipeline(renderPipeline);
        RenderState state;
        state.viewports[0] = Viewport::fromSize(kWidth, kHeight);
        state.viewportCount = 1;
        state.scissorRects[0] = ScissorRect::fromSize(kWidth, kHeight);
        state.scissorRectCount = 1;
        state.vertexBuffers[0] = vertexBuffer;
        state.vertexBufferCount = 1;
        passEncoder->setRenderState(state);

        DrawArguments args;
        args.vertexCount = 3;
        passEncoder->draw(args);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    void runGraphicsPipeline()
    {
        createGraphicsResources();
        createGraphicsPipeline();
        dispatchGraphicsPipeline();
        freeGraphicsResources();
    }

    void runTests()
    {
        // Cache is cold and we expect 2 misses (2 entry points).
        createDevice();
        runGraphicsPipeline();
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 2);

        // Cache is hot and we expect 2 hits.
        createDevice();
        runGraphicsPipeline();
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 2);
        CHECK_EQ(getStats().entryCount, 2);
    }
};

// Similar to ShaderCacheTestGraphics but with two separate shader files for the vertex and fragment shaders.
struct ShaderCacheTestGraphicsSplit : ShaderCacheTestGraphics
{
    void createGraphicsPipeline()
    {
        ComPtr<slang::ISession> slangSession;
        REQUIRE_CALL(device->getSlangSession(slangSession.writeRef()));
        slang::IModule* vertexModule = slangSession->loadModule("test-shader-cache-graphics-vertex");
        REQUIRE(vertexModule != nullptr);
        slang::IModule* fragmentModule = slangSession->loadModule("test-shader-cache-graphics-fragment");
        REQUIRE(fragmentModule != nullptr);

        ComPtr<slang::IEntryPoint> vertexEntryPoint;
        REQUIRE_CALL(vertexModule->findEntryPointByName("main", vertexEntryPoint.writeRef()));

        ComPtr<slang::IEntryPoint> fragmentEntryPoint;
        REQUIRE_CALL(fragmentModule->findEntryPointByName("main", fragmentEntryPoint.writeRef()));

        std::vector<slang::IComponentType*> componentTypes;
        componentTypes.push_back(vertexModule);
        componentTypes.push_back(fragmentModule);

        ComPtr<slang::IComponentType> composedProgram;
        REQUIRE_CALL(slangSession->createCompositeComponentType(
            componentTypes.data(),
            componentTypes.size(),
            composedProgram.writeRef()
        ));

        std::vector<slang::IComponentType*> entryPoints;
        entryPoints.push_back(vertexEntryPoint);
        entryPoints.push_back(fragmentEntryPoint);

        ShaderProgramDesc programDesc = {};
        programDesc.slangGlobalScope = composedProgram.get();
        programDesc.linkingStyle = LinkingStyle::SeparateEntryPointCompilation;
        programDesc.slangEntryPoints = entryPoints.data();
        programDesc.slangEntryPointCount = 2;

        ComPtr<IShaderProgram> shaderProgram = device->createShaderProgram(programDesc);

        ColorTargetDesc target;
        target.format = format;
        RenderPipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        pipelineDesc.inputLayout = inputLayout;
        pipelineDesc.targets = &target;
        pipelineDesc.targetCount = 1;
        pipelineDesc.depthStencil.depthTestEnable = false;
        pipelineDesc.depthStencil.depthWriteEnable = false;
        REQUIRE_CALL(device->createRenderPipeline(pipelineDesc, renderPipeline.writeRef()));
    }

    void runGraphicsPipeline()
    {
        createGraphicsResources();
        createGraphicsPipeline();
        dispatchGraphicsPipeline();
        freeGraphicsResources();
    }

    void runTests()
    {
        // Cache is cold and we expect 2 misses (2 entry points).
        createDevice();
        runGraphicsPipeline();
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 2);

        // Cache is hot and we expect 2 hits.
        createDevice();
        runGraphicsPipeline();
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 2);
        CHECK_EQ(getStats().entryCount, 2);
    }
};

template<typename T>
void runTest(GpuTestContext* ctx)
{
    std::string tempDirectory = getCaseTempDirectory();
    T test;
    test.run(ctx, tempDirectory);
}

// TODO
// These tests are super expensive because they re-create devices.
// This is needed because slang doesn't support reloading modules at this time.

GPU_TEST_CASE("shader-cache-source-file", D3D12 | Vulkan | DontCreateDevice)
{
    runTest<ShaderCacheTestSourceFile>(ctx);
}

GPU_TEST_CASE("shader-cache-source-string", D3D12 | Vulkan | DontCreateDevice)
{
    runTest<ShaderCacheTestSourceString>(ctx);
}

GPU_TEST_CASE("shader-cache-entry-point", D3D12 | Vulkan | DontCreateDevice)
{
    runTest<ShaderCacheTestEntryPoint>(ctx);
}

GPU_TEST_CASE("shader-cache-import-include", D3D12 | Vulkan | DontCreateDevice)
{
    runTest<ShaderCacheTestImportInclude>(ctx);
}

GPU_TEST_CASE("shader-cache-specialization", D3D12 | Vulkan | DontCreateDevice)
{
    runTest<ShaderCacheTestSpecialization>(ctx);
}

GPU_TEST_CASE("shader-cache-eviction", D3D12 | Vulkan | DontCreateDevice)
{
    runTest<ShaderCacheTestEviction>(ctx);
}

GPU_TEST_CASE("shader-cache-graphics", D3D12 | Vulkan | DontCreateDevice)
{
    runTest<ShaderCacheTestGraphics>(ctx);
}

GPU_TEST_CASE("shader-cache-graphics-split", D3D12 | Vulkan | DontCreateDevice)
{
    runTest<ShaderCacheTestGraphicsSplit>(ctx);
}
