#include "testing.h"

#include <map>
#include <filesystem>
#include <fstream>

using namespace gfx;
using namespace gfx::testing;

class VirtualShaderCache : public IPersistentShaderCache
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
            auto it = std::min_element(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.second.ticket < b.second.ticket; });
            entries.erase(it);
        }

        Key key(static_cast<const uint8_t*>(key_->getBufferPointer()), static_cast<const uint8_t*>(key_->getBufferPointer()) + key_->getBufferSize());
        Data data(static_cast<const uint8_t*>(data_->getBufferPointer()), static_cast<const uint8_t*>(data_->getBufferPointer()) + data_->getBufferSize());
        entries[key] = { ticketCounter++, data };
        stats.entryCount = entries.size();
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL queryCache(ISlangBlob* key_, ISlangBlob** outData) override
    {
        Key key(static_cast<const uint8_t*>(key_->getBufferPointer()), static_cast<const uint8_t*>(key_->getBufferPointer()) + key_->getBufferSize());
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

    virtual SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override
    {
        if (uuid == SlangUUID SLANG_UUID_IPersistentShaderCache) {
            *outObject = static_cast<gfx::IPersistentShaderCache*>(this);
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
// Because of this, the tests recreate a GFX device for each test step,
// allowing to modify shader sources in between.
struct ShaderCacheTest
{
    GpuTestContext* ctx;
    DeviceType deviceType;
    std::filesystem::path tempDirectory;
    
    ComPtr<IDevice> device;
    ComPtr<IPipelineState> pipelineState;
    ComPtr<IBufferResource> bufferResource;
    ComPtr<IResourceView> bufferView;

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
        )");    

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
        )");

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
        )");


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
        gfx::IDevice::Desc deviceDesc = {};
        deviceDesc.deviceType = deviceType;
        deviceDesc.slang.slangGlobalSession = ctx->slangGlobalSession;
        auto searchPaths = getSlangSearchPaths();
        std::string tempDirectoryStr = tempDirectory.string();
        searchPaths.push_back(tempDirectoryStr.c_str());
        deviceDesc.slang.searchPaths = searchPaths.data();
        deviceDesc.slang.searchPathCount = searchPaths.size();
        deviceDesc.persistentShaderCache = &shaderCache;

        gfx::D3D12DeviceExtendedDesc extDesc = {};
        extDesc.rootParameterShaderAttributeName = "root";
        
        gfx::SlangSessionExtendedDesc slangExtDesc = {};
        std::vector<slang::CompilerOptionEntry> entries;
        slang::CompilerOptionEntry emitSpirvDirectlyEntry;
        emitSpirvDirectlyEntry.name = slang::CompilerOptionName::EmitSpirvDirectly;
        emitSpirvDirectlyEntry.value.intValue0 = 1;
        entries.push_back(emitSpirvDirectlyEntry);
#if GFX_ENABLE_SPIRV_DEBUG
        slang::CompilerOptionEntry debugLevelCompilerOptionEntry;
        debugLevelCompilerOptionEntry.name = slang::CompilerOptionName::DebugInformation;
        debugLevelCompilerOptionEntry.value.intValue0 = SLANG_DEBUG_INFO_LEVEL_STANDARD;
        entries.push_back(debugLevelCompilerOptionEntry);
#endif
        slangExtDesc.compilerOptionEntries = entries.data();
        slangExtDesc.compilerOptionEntryCount = entries.size();

        deviceDesc.extendedDescCount = 2;
        void* extDescPtrs[2] = { &extDesc, &slangExtDesc };
        deviceDesc.extendedDescs = extDescPtrs;

        // TODO: We should also set the debug callback
        // (And in general reduce the differences (and duplication) between
        // here and render-test-main.cpp)
#ifdef _DEBUG
        gfx::gfxEnableDebugLayer();
#endif

        GFX_CHECK_CALL_ABORT(gfxCreateDevice(&deviceDesc, device.writeRef()));
    }

    void createComputeResources()
    {
        const int numberCount = 4;
        float initialData[] = { 0.0f, 1.0f, 2.0f, 3.0f };
        IBufferResource::Desc bufferDesc = {};
        bufferDesc.sizeInBytes = numberCount * sizeof(float);
        bufferDesc.format = Format::Unknown;
        bufferDesc.elementSize = sizeof(float);
        bufferDesc.allowedStates = ResourceStateSet(
            ResourceState::ShaderResource,
            ResourceState::UnorderedAccess,
            ResourceState::CopyDestination,
            ResourceState::CopySource);
        bufferDesc.defaultState = ResourceState::UnorderedAccess;
        bufferDesc.memoryType = MemoryType::DeviceLocal;

        GFX_CHECK_CALL_ABORT(device->createBufferResource(
            bufferDesc,
            (void*)initialData,
            bufferResource.writeRef()));

        IResourceView::Desc viewDesc = {};
        viewDesc.type = IResourceView::Type::UnorderedAccess;
        viewDesc.format = Format::Unknown;
        GFX_CHECK_CALL_ABORT(
            device->createBufferView(bufferResource, nullptr, viewDesc, bufferView.writeRef()));
    }

    void freeComputeResources()
    {
        bufferResource = nullptr;
        bufferView = nullptr;
        pipelineState = nullptr;
    }

    void createComputePipeline(const char* moduleName, const char* entryPointName)
    {
        ComPtr<IShaderProgram> shaderProgram;
        slang::ProgramLayout* slangReflection;
        GFX_CHECK_CALL_ABORT(loadComputeProgram(device, shaderProgram, moduleName, entryPointName, slangReflection));

        ComputePipelineStateDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        GFX_CHECK_CALL_ABORT(
            device->createComputePipelineState(pipelineDesc, pipelineState.writeRef()));
    }

    void createComputePipeline(std::string shaderSource)
    {
        ComPtr<IShaderProgram> shaderProgram;
        GFX_CHECK_CALL_ABORT(loadComputeProgramFromSource(device, shaderProgram, shaderSource));

        ComputePipelineStateDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        GFX_CHECK_CALL_ABORT(
            device->createComputePipelineState(pipelineDesc, pipelineState.writeRef()));
    }

    void dispatchComputePipeline()
    {
        ComPtr<ITransientResourceHeap> transientHeap;
        ITransientResourceHeap::Desc transientHeapDesc = {};
        transientHeapDesc.constantBufferSize = 4096;
        GFX_CHECK_CALL_ABORT(
            device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

        ICommandQueue::Desc queueDesc = { ICommandQueue::QueueType::Graphics };
        auto queue = device->createCommandQueue(queueDesc);

        auto commandBuffer = transientHeap->createCommandBuffer();
        auto encoder = commandBuffer->encodeComputeCommands();

        auto rootObject = encoder->bindPipeline(pipelineState);

        // Bind buffer view to the entry point.
        ShaderCursor entryPointCursor(rootObject->getEntryPoint(0));
        entryPointCursor.getPath("buffer").setResource(bufferView);

        encoder->dispatchCompute(4, 1, 1);
        encoder->endEncoding();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }        

    bool checkOutput(const std::vector<float>& expectedOutput)
    {
        ComPtr<ISlangBlob> bufferBlob;
        device->readBufferResource(bufferResource, 0, 4 * sizeof(float), bufferBlob.writeRef());
        REQUIRE(bufferBlob);
        REQUIRE(bufferBlob->getBufferSize() == expectedOutput.size() * sizeof(float));
        return ::memcmp(bufferBlob->getBufferPointer(), expectedOutput.data(), bufferBlob->getBufferSize()) == 0;
    }

    void runComputePipeline(const char* moduleName, const char* entryPointName, const std::vector<float>& expectedOutput)
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

    VirtualShaderCache::Stats getStats()
    {
        return shaderCache.stats;
    }

    void run(GpuTestContext* ctx_, DeviceType deviceType_, std::string tempDirectory_)
    {
        ctx = ctx_;
        deviceType = deviceType_;
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
        runComputePipeline("shader-cache-tmp-a", "main", { 1.f, 2.f, 3.f, 4.f });
        runComputePipeline("shader-cache-tmp-b", "main", { 2.f, 3.f, 4.f, 5.f });
        runComputePipeline("shader-cache-tmp-c", "main", { 3.f, 4.f, 5.f, 6.f });
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 3);

        // Cache is hot and we expect 3 hits.
        createDevice();
        runComputePipeline("shader-cache-tmp-a", "main", { 1.f, 2.f, 3.f, 4.f });
        runComputePipeline("shader-cache-tmp-b", "main", { 2.f, 3.f, 4.f, 5.f });
        runComputePipeline("shader-cache-tmp-c", "main", { 3.f, 4.f, 5.f, 6.f });
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 3);
        CHECK_EQ(getStats().entryCount, 3);

        // Write shader source files, all rotated by one.
        writeShader(computeShaderA, "shader-cache-tmp-b.slang");
        writeShader(computeShaderB, "shader-cache-tmp-c.slang");
        writeShader(computeShaderC, "shader-cache-tmp-a.slang");

        // Cache is cold again and we expect 3 misses.
        createDevice();
        runComputePipeline("shader-cache-tmp-b", "main", { 1.f, 2.f, 3.f, 4.f });
        runComputePipeline("shader-cache-tmp-c", "main", { 2.f, 3.f, 4.f, 5.f });
        runComputePipeline("shader-cache-tmp-a", "main", { 3.f, 4.f, 5.f, 6.f });
        CHECK_EQ(getStats().missCount, 6);
        CHECK_EQ(getStats().hitCount, 3);
        CHECK_EQ(getStats().entryCount, 6);

        // Cache is hot again and we expect 3 hits.
        createDevice();
        runComputePipeline("shader-cache-tmp-b", "main", { 1.f, 2.f, 3.f, 4.f });
        runComputePipeline("shader-cache-tmp-c", "main", { 2.f, 3.f, 4.f, 5.f });
        runComputePipeline("shader-cache-tmp-a", "main", { 3.f, 4.f, 5.f, 6.f });
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
        runComputePipeline(computeShaderA, { 1.f, 2.f, 3.f, 4.f });
        runComputePipeline(computeShaderB, { 2.f, 3.f, 4.f, 5.f });
        runComputePipeline(computeShaderC, { 3.f, 4.f, 5.f, 6.f });
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 3);

        // Cache is hot and we expect 3 hits.
        createDevice();
        runComputePipeline(computeShaderA, { 1.f, 2.f, 3.f, 4.f });
        runComputePipeline(computeShaderB, { 2.f, 3.f, 4.f, 5.f });
        runComputePipeline(computeShaderC, { 3.f, 4.f, 5.f, 6.f });
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
        runComputePipeline("test-shader-cache-multiple-entry-points", "computeA", { 1.f, 2.f, 3.f, 4.f });
        runComputePipeline("test-shader-cache-multiple-entry-points", "computeB", { 2.f, 3.f, 4.f, 5.f });
        runComputePipeline("test-shader-cache-multiple-entry-points", "computeC", { 3.f, 4.f, 5.f, 6.f });
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 3);

        // Cache is hot and we expect 3 hits.
        createDevice();
        runComputePipeline("test-shader-cache-multiple-entry-points", "computeA", { 1.f, 2.f, 3.f, 4.f });
        runComputePipeline("test-shader-cache-multiple-entry-points", "computeB", { 2.f, 3.f, 4.f, 5.f });
        runComputePipeline("test-shader-cache-multiple-entry-points", "computeC", { 3.f, 4.f, 5.f, 6.f });
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
        )");

    std::string importedContentsB = std::string(
        R"(
        public void processElement(RWStructuredBuffer<float> buffer, uint index)
        {
            var input = buffer[index];
            buffer[index] = input + 2.0f;
        }
        )");

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
        )");

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
        })");

    void runTests()
    {
        // Write shader source files.
        writeShader(importedContentsA, "shader-cache-tmp-imported.slang");
        writeShader(importFile, "shader-cache-tmp-import.slang");
        writeShader(includeFile, "shader-cache-tmp-include.slang");

        // Cache is cold and we expect 2 misses.
        createDevice();
        runComputePipeline("shader-cache-tmp-import", "main", { 1.f, 2.f, 3.f, 4.f });
        runComputePipeline("shader-cache-tmp-include", "main", { 1.f, 2.f, 3.f, 4.f });
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 2);

        // Cache is hot and we expect 2 hits.
        createDevice();
        runComputePipeline("shader-cache-tmp-import", "main", { 1.f, 2.f, 3.f, 4.f });
        runComputePipeline("shader-cache-tmp-include", "main", { 1.f, 2.f, 3.f, 4.f });
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 2);
        CHECK_EQ(getStats().entryCount, 2);

        // Change content of imported/included shader file.
        writeShader(importedContentsB, "shader-cache-tmp-imported.slang");

        // Cache is cold and we expect 2 misses.
        createDevice();
        runComputePipeline("shader-cache-tmp-import", "main", { 2.f, 3.f, 4.f, 5.f });
        runComputePipeline("shader-cache-tmp-include", "main", { 2.f, 3.f, 4.f, 5.f });
        CHECK_EQ(getStats().missCount, 4);
        CHECK_EQ(getStats().hitCount, 2);
        CHECK_EQ(getStats().entryCount, 4);

        // Cache is hot and we expect 2 hits.
        createDevice();
        runComputePipeline("shader-cache-tmp-import", "main", { 2.f, 3.f, 4.f, 5.f });
        runComputePipeline("shader-cache-tmp-include", "main", { 2.f, 3.f, 4.f, 5.f });
        CHECK_EQ(getStats().missCount, 4);
        CHECK_EQ(getStats().hitCount, 4);
        CHECK_EQ(getStats().entryCount, 4);
    }
};

// One shader featuring multiple kinds of shader objects that can be bound.
struct ShaderCacheTestSpecialization : ShaderCacheTest
{
    slang::ProgramLayout* slangReflection;

    void createComputePipeline()
    {
        ComPtr<IShaderProgram> shaderProgram;

        GFX_CHECK_CALL_ABORT(
            loadComputeProgram(device, shaderProgram, "test-shader-cache-specialization", "computeMain", slangReflection));

        ComputePipelineStateDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        GFX_CHECK_CALL_ABORT(
            device->createComputePipelineState(pipelineDesc, pipelineState.writeRef()));
    }        

    void dispatchComputePipeline(const char* transformerTypeName)
    {
        ComPtr<ITransientResourceHeap> transientHeap;
        ITransientResourceHeap::Desc transientHeapDesc = {};
        transientHeapDesc.constantBufferSize = 4096;
        GFX_CHECK_CALL_ABORT(
            device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

        ICommandQueue::Desc queueDesc = { ICommandQueue::QueueType::Graphics };
        auto queue = device->createCommandQueue(queueDesc);

        auto commandBuffer = transientHeap->createCommandBuffer();
        auto encoder = commandBuffer->encodeComputeCommands();

        auto rootObject = encoder->bindPipeline(pipelineState);

        ComPtr<IShaderObject> transformer;
        slang::TypeReflection* transformerType = slangReflection->findTypeByName(transformerTypeName);
        GFX_CHECK_CALL_ABORT(device->createShaderObject(
            transformerType, ShaderObjectContainerType::None, transformer.writeRef()));

        float c = 5.f;
        ShaderCursor(transformer).getPath("c").setData(&c, sizeof(float));

        ShaderCursor entryPointCursor(rootObject->getEntryPoint(0));
        entryPointCursor.getPath("buffer").setResource(bufferView);
        entryPointCursor.getPath("transformer").setObject(transformer);

        encoder->dispatchCompute(1, 1, 1);
        encoder->endEncoding();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
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
        runComputePipeline("AddTransformer", { 5.f, 6.f, 7.f, 8.f });
        runComputePipeline("MulTransformer", { 0.f, 5.f, 10.f, 15.f });
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 2);

        // Cache is hot and we expect 2 hits.
        createDevice();
        runComputePipeline("AddTransformer", { 5.f, 6.f, 7.f, 8.f });
        runComputePipeline("MulTransformer", { 0.f, 5.f, 10.f, 15.f });
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
        runComputePipeline(computeShaderA, { 1.f, 2.f, 3.f, 4.f });
        runComputePipeline(computeShaderB, { 2.f, 3.f, 4.f, 5.f });
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 0);
        CHECK_EQ(getStats().entryCount, 2);

        // Load shader A & B. Cache is hot and we expect 2 hits.
        createDevice();
        runComputePipeline(computeShaderA, { 1.f, 2.f, 3.f, 4.f });
        runComputePipeline(computeShaderB, { 2.f, 3.f, 4.f, 5.f });
        CHECK_EQ(getStats().missCount, 2);
        CHECK_EQ(getStats().hitCount, 2);
        CHECK_EQ(getStats().entryCount, 2);

        // Load shader C. Cache is cold and we expect 1 miss.
        // This will evict the least frequently used entry (shader A).
        // We expect 2 entries in the cache (shader B & C).
        createDevice();
        runComputePipeline(computeShaderC, { 3.f, 4.f, 5.f, 6.f });
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 2);
        CHECK_EQ(getStats().entryCount, 2);

        // Load shader C. Cache is hot and we expect 1 hit.
        createDevice();
        runComputePipeline(computeShaderC, { 3.f, 4.f, 5.f, 6.f });
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 3);
        CHECK_EQ(getStats().entryCount, 2);

        // Load shader B. Cache is hot and we expect 1 hit.
        createDevice();
        runComputePipeline(computeShaderB, { 2.f, 3.f, 4.f, 5.f });
        CHECK_EQ(getStats().missCount, 3);
        CHECK_EQ(getStats().hitCount, 4);
        CHECK_EQ(getStats().entryCount, 2);

        // Load shader A. Cache is cold and we expect 1 miss.
        createDevice();
        runComputePipeline(computeShaderA, { 1.f, 2.f, 3.f, 4.f });
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
    static const Format format = Format::R32G32B32A32_FLOAT;

    ComPtr<IBufferResource> vertexBuffer;
    ComPtr<ITextureResource> colorBuffer;
    ComPtr<IInputLayout> inputLayout;
    ComPtr<IFramebufferLayout> framebufferLayout;
    ComPtr<IRenderPassLayout> renderPass;
    ComPtr<IFramebuffer> framebuffer;

    ComPtr<IBufferResource> createVertexBuffer(IDevice* device)
    {
        const Vertex vertices[] = {
            { 0, 0, 0.5 },
            { 1, 0, 0.5 },
            { 0, 1, 0.5 },
        };

        IBufferResource::Desc vertexBufferDesc;
        vertexBufferDesc.type = IResource::Type::Buffer;
        vertexBufferDesc.sizeInBytes = sizeof(vertices);
        vertexBufferDesc.defaultState = ResourceState::VertexBuffer;
        vertexBufferDesc.allowedStates = ResourceState::VertexBuffer;
        ComPtr<IBufferResource> vertexBuffer = device->createBufferResource(vertexBufferDesc, vertices);
        REQUIRE(vertexBuffer != nullptr);
        return vertexBuffer;
    }

    ComPtr<ITextureResource> createColorBuffer(IDevice* device)
    {
        gfx::ITextureResource::Desc colorBufferDesc;
        colorBufferDesc.type = IResource::Type::Texture2D;
        colorBufferDesc.size.width = kWidth;
        colorBufferDesc.size.height = kHeight;
        colorBufferDesc.size.depth = 1;
        colorBufferDesc.numMipLevels = 1;
        colorBufferDesc.format = format;
        colorBufferDesc.defaultState = ResourceState::RenderTarget;
        colorBufferDesc.allowedStates = { ResourceState::RenderTarget, ResourceState::CopySource };
        ComPtr<ITextureResource> colorBuffer = device->createTextureResource(colorBufferDesc, nullptr);
        REQUIRE(colorBuffer != nullptr);
        return colorBuffer;
    }

    void createGraphicsResources()
    {
        VertexStreamDesc vertexStreams[] = {
            { sizeof(Vertex), InputSlotClass::PerVertex, 0 },
        };

        InputElementDesc inputElements[] = {
            // Vertex buffer data
            { "POSITION", 0, Format::R32G32B32_FLOAT, offsetof(Vertex, position), 0 },
        };
        IInputLayout::Desc inputLayoutDesc = {};
        inputLayoutDesc.inputElementCount = SLANG_COUNT_OF(inputElements);
        inputLayoutDesc.inputElements = inputElements;
        inputLayoutDesc.vertexStreamCount = SLANG_COUNT_OF(vertexStreams);
        inputLayoutDesc.vertexStreams = vertexStreams;
        inputLayout = device->createInputLayout(inputLayoutDesc);
        REQUIRE(inputLayout != nullptr);

        vertexBuffer = createVertexBuffer(device);
        colorBuffer = createColorBuffer(device);

        IFramebufferLayout::TargetLayout targetLayout;
        targetLayout.format = format;
        targetLayout.sampleCount = 1;

        IFramebufferLayout::Desc framebufferLayoutDesc;
        framebufferLayoutDesc.renderTargetCount = 1;
        framebufferLayoutDesc.renderTargets = &targetLayout;
        framebufferLayout = device->createFramebufferLayout(framebufferLayoutDesc);
        REQUIRE(framebufferLayout != nullptr);

        IRenderPassLayout::Desc renderPassDesc = {};
        renderPassDesc.framebufferLayout = framebufferLayout;
        renderPassDesc.renderTargetCount = 1;
        IRenderPassLayout::TargetAccessDesc renderTargetAccess = {};
        renderTargetAccess.loadOp = IRenderPassLayout::TargetLoadOp::Clear;
        renderTargetAccess.storeOp = IRenderPassLayout::TargetStoreOp::Store;
        renderTargetAccess.initialState = ResourceState::RenderTarget;
        renderTargetAccess.finalState = ResourceState::CopySource;
        renderPassDesc.renderTargetAccess = &renderTargetAccess;
        GFX_CHECK_CALL_ABORT(device->createRenderPassLayout(renderPassDesc, renderPass.writeRef()));

        gfx::IResourceView::Desc colorBufferViewDesc;
        memset(&colorBufferViewDesc, 0, sizeof(colorBufferViewDesc));
        colorBufferViewDesc.format = format;
        colorBufferViewDesc.renderTarget.shape = gfx::IResource::Type::Texture2D;
        colorBufferViewDesc.type = gfx::IResourceView::Type::RenderTarget;
        auto rtv = device->createTextureView(colorBuffer, colorBufferViewDesc);

        gfx::IFramebuffer::Desc framebufferDesc;
        framebufferDesc.renderTargetCount = 1;
        framebufferDesc.depthStencilView = nullptr;
        framebufferDesc.renderTargetViews = rtv.readRef();
        framebufferDesc.layout = framebufferLayout;
        GFX_CHECK_CALL_ABORT(device->createFramebuffer(framebufferDesc, framebuffer.writeRef()));
    }

    void freeGraphicsResources()
    {
        inputLayout = nullptr;
        framebufferLayout = nullptr;
        renderPass = nullptr;
        framebuffer = nullptr;
        vertexBuffer = nullptr;
        colorBuffer = nullptr;
        pipelineState = nullptr;
    }

    void createGraphicsPipeline()
    {
        ComPtr<IShaderProgram> shaderProgram;
        slang::ProgramLayout* slangReflection;
        GFX_CHECK_CALL_ABORT(
            loadGraphicsProgram(device, shaderProgram, "test-shader-cache-graphics", "vertexMain", "fragmentMain", slangReflection));

        GraphicsPipelineStateDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        pipelineDesc.inputLayout = inputLayout;
        pipelineDesc.framebufferLayout = framebufferLayout;
        pipelineDesc.depthStencil.depthTestEnable = false;
        pipelineDesc.depthStencil.depthWriteEnable = false;
        GFX_CHECK_CALL_ABORT(
            device->createGraphicsPipelineState(pipelineDesc, pipelineState.writeRef()));
    }

    void dispatchGraphicsPipeline()
    {
        ComPtr<ITransientResourceHeap> transientHeap;
        ITransientResourceHeap::Desc transientHeapDesc = {};
        transientHeapDesc.constantBufferSize = 4096;
        GFX_CHECK_CALL_ABORT(
            device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

        ICommandQueue::Desc queueDesc = { ICommandQueue::QueueType::Graphics };
        auto queue = device->createCommandQueue(queueDesc);
        auto commandBuffer = transientHeap->createCommandBuffer();

        auto encoder = commandBuffer->encodeRenderCommands(renderPass, framebuffer);
        auto rootObject = encoder->bindPipeline(pipelineState);

        gfx::Viewport viewport = {};
        viewport.maxZ = 1.0f;
        viewport.extentX = (float)kWidth;
        viewport.extentY = (float)kHeight;
        encoder->setViewportAndScissor(viewport);

        encoder->setVertexBuffer(0, vertexBuffer);
        encoder->setPrimitiveTopology(PrimitiveTopology::TriangleList);

        encoder->draw(3);
        encoder->endEncoding();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
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
        GFX_CHECK_CALL_ABORT(device->getSlangSession(slangSession.writeRef()));
        slang::IModule* vertexModule = slangSession->loadModule("test-shader-cache-graphics-vertex");
        REQUIRE(vertexModule != nullptr);
        slang::IModule* fragmentModule = slangSession->loadModule("test-shader-cache-graphics-fragment");
        REQUIRE(fragmentModule != nullptr);

        ComPtr<slang::IEntryPoint> vertexEntryPoint;
        GFX_CHECK_CALL_ABORT(
            vertexModule->findEntryPointByName("main", vertexEntryPoint.writeRef()));

        ComPtr<slang::IEntryPoint> fragmentEntryPoint;
        GFX_CHECK_CALL_ABORT(
            fragmentModule->findEntryPointByName("main", fragmentEntryPoint.writeRef()));

        std::vector<slang::IComponentType*> componentTypes;
        componentTypes.push_back(vertexModule);
        componentTypes.push_back(fragmentModule);

        ComPtr<slang::IComponentType> composedProgram;
        GFX_CHECK_CALL_ABORT(
            slangSession->createCompositeComponentType(
                componentTypes.data(),
                componentTypes.size(),
                composedProgram.writeRef()));

        slang::ProgramLayout* slangReflection = composedProgram->getLayout();

        std::vector<slang::IComponentType*> entryPoints;
        entryPoints.push_back(vertexEntryPoint);
        entryPoints.push_back(fragmentEntryPoint);

        gfx::IShaderProgram::Desc programDesc = {};
        programDesc.slangGlobalScope = composedProgram.get();
        programDesc.linkingStyle = gfx::IShaderProgram::LinkingStyle::SeparateEntryPointCompilation;
        programDesc.entryPointCount = 2;
        programDesc.slangEntryPoints = entryPoints.data();

        ComPtr<IShaderProgram> shaderProgram = device->createProgram(programDesc);

        GraphicsPipelineStateDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        pipelineDesc.inputLayout = inputLayout;
        pipelineDesc.framebufferLayout = framebufferLayout;
        pipelineDesc.depthStencil.depthTestEnable = false;
        pipelineDesc.depthStencil.depthWriteEnable = false;
        GFX_CHECK_CALL_ABORT(
            device->createGraphicsPipelineState(pipelineDesc, pipelineState.writeRef()));
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
void runTest(GpuTestContext* ctx, DeviceType deviceType)
{
    std::string tempDirectory = getCaseTempDirectory();
    T test;
    test.run(ctx, deviceType, tempDirectory);
}

// TODO 
// These tests are super expensive because they re-create devices.
// This is needed because slang doesn't support reloading modules at this time.

TEST_CASE("shader-cache-source-file")
{
    runGpuTests(runTest<ShaderCacheTestSourceFile>, {DeviceType::D3D12, DeviceType::Vulkan});
}

TEST_CASE("shader-cache-source-string")
{
    runGpuTests(runTest<ShaderCacheTestSourceString>, {DeviceType::D3D12, DeviceType::Vulkan});
}

TEST_CASE("shader-cache-entry-point")
{
    runGpuTests(runTest<ShaderCacheTestEntryPoint>, {DeviceType::D3D12, DeviceType::Vulkan});
}

TEST_CASE("shader-cache-import-include")
{
    runGpuTests(runTest<ShaderCacheTestImportInclude>, {DeviceType::D3D12, DeviceType::Vulkan});
}

TEST_CASE("shader-cache-specialization")
{
    runGpuTests(runTest<ShaderCacheTestSpecialization>, {DeviceType::D3D12, DeviceType::Vulkan});
}

TEST_CASE("shader-cache-eviction")
{
    runGpuTests(runTest<ShaderCacheTestEviction>, {DeviceType::D3D12, DeviceType::Vulkan});
}

TEST_CASE("shader-cache-graphics")
{
    runGpuTests(runTest<ShaderCacheTestEviction>, {DeviceType::D3D12, DeviceType::Vulkan});
}

TEST_CASE("shader-cache-graphics-split")
{
    runGpuTests(runTest<ShaderCacheTestGraphicsSplit>, {DeviceType::D3D12, DeviceType::Vulkan});
}
