#pragma once

#include <slang-rhi.h>

#include "slang-context.h"

#include "core/common.h"
#include "core/short_vector.h"

#include "staging-heap.h"

#include "rhi-shared-fwd.h"

#include <map>
#include <unordered_map>

namespace rhi {

struct ComponentKey
{
    std::string typeName;
    short_vector<ShaderComponentID> specializationArgs;
    size_t hash;
    void updateHash()
    {
        hash = std::hash<std::string_view>()(typeName);
        for (auto& arg : specializationArgs)
            hash_combine(hash, arg);
    }
    template<typename KeyType>
    bool operator==(const KeyType& other) const
    {
        if (typeName != other.typeName)
            return false;
        if (specializationArgs.size() != other.specializationArgs.size())
            return false;
        for (size_t i = 0; i < other.specializationArgs.size(); i++)
        {
            if (specializationArgs[i] != other.specializationArgs[i])
                return false;
        }
        return true;
    }
};

struct PipelineKey
{
    Pipeline* pipeline;
    short_vector<ShaderComponentID> specializationArgs;
    size_t hash;
    void updateHash()
    {
        hash = std::hash<void*>()(pipeline);
        for (auto& arg : specializationArgs)
            hash_combine(hash, arg);
    }
    bool operator==(const PipelineKey& other) const
    {
        if (pipeline != other.pipeline)
            return false;
        if (specializationArgs.size() != other.specializationArgs.size())
            return false;
        for (size_t i = 0; i < other.specializationArgs.size(); i++)
        {
            if (specializationArgs[i] != other.specializationArgs[i])
                return false;
        }
        return true;
    }
};

// A cache from specialization keys to a specialized `ShaderKernel`.
class ShaderCache : public RefObject
{
public:
    ShaderComponentID getComponentId(slang::TypeReflection* type);
    ShaderComponentID getComponentId(std::string_view name);
    ShaderComponentID getComponentId(ComponentKey key);

    RefPtr<Pipeline> getSpecializedPipeline(PipelineKey programKey)
    {
        auto it = specializedPipelines.find(programKey);
        if (it != specializedPipelines.end())
            return it->second;
        return nullptr;
    }

    void addSpecializedPipeline(PipelineKey key, RefPtr<Pipeline> specializedPipeline);

    void free()
    {
        componentIds = decltype(componentIds)();
        specializedPipelines = decltype(specializedPipelines)();
    }

    size_t getSize() const { return specializedPipelines.size(); }

protected:
    struct ComponentKeyHasher
    {
        std::size_t operator()(const ComponentKey& k) const { return k.hash; }
    };
    struct PipelineKeyHasher
    {
        std::size_t operator()(const PipelineKey& k) const { return k.hash; }
    };

    std::unordered_map<ComponentKey, ShaderComponentID, ComponentKeyHasher> componentIds;
    std::unordered_map<PipelineKey, RefPtr<Pipeline>, PipelineKeyHasher> specializedPipelines;
};

class NullDebugCallback : public IDebugCallback
{
public:
    virtual SLANG_NO_THROW void SLANG_MCALL
    handleMessage(DebugMessageType type, DebugMessageSource source, const char* message) override
    {
        SLANG_UNUSED(type);
        SLANG_UNUSED(source);
        SLANG_UNUSED(message);
    }

    static IDebugCallback* getInstance()
    {
        static NullDebugCallback instance;
        return &instance;
    }
};

// Device implementation shared by all platforms.
// Responsible for shader compilation, specialization and caching.
class Device : public IDevice, public ComObject
{
public:
    using IDevice::readTexture;
    using IDevice::readBuffer;

    SLANG_COM_OBJECT_IUNKNOWN_ADD_REF
    SLANG_COM_OBJECT_IUNKNOWN_RELEASE

    virtual SLANG_NO_THROW const DeviceInfo& SLANG_MCALL getInfo() const override { return m_info; }
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeDeviceHandles(DeviceNativeHandles* outHandles) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getFeatures(uint32_t* outFeatureCount, Feature* outFeatures) override;
    virtual SLANG_NO_THROW bool SLANG_MCALL hasFeature(Feature feature) override;
    virtual SLANG_NO_THROW bool SLANG_MCALL hasFeature(const char* feature) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    getCapabilities(uint32_t* outCapabilityCount, Capability* outCapabilities) override;
    virtual SLANG_NO_THROW bool SLANG_MCALL hasCapability(Capability capability) override;
    virtual SLANG_NO_THROW bool SLANG_MCALL hasCapability(const char* capability) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getFormatSupport(Format format, FormatSupport* outFormatSupport) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSlangSession(slang::ISession** outSlangSession) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(const SlangUUID& uuid, void** outObject) override;
    IDevice* getInterface(const Guid& guid);

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTextureFromNativeHandle(NativeHandle handle, const TextureDesc& desc, ITexture** outTexture) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTextureFromSharedHandle(NativeHandle handle, const TextureDesc& desc, const Size size, ITexture** outTexture)
        override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBufferFromSharedHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createInputLayout(const InputLayoutDesc& desc, IInputLayout** outLayout) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRenderPipeline(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createComputePipeline(const ComputePipelineDesc& desc, IComputePipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRayTracingPipeline(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderObject(
        slang::ISession* session,
        slang::TypeReflection* type,
        ShaderObjectContainerType containerType,
        IShaderObject** outObject
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createShaderObjectFromTypeLayout(slang::TypeLayoutReflection* typeLayout, IShaderObject** outObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRootShaderObject(IShaderProgram* program, IShaderObject** outObject) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE for platforms
    // without ray tracing support.
    virtual SLANG_NO_THROW Result SLANG_MCALL getAccelerationStructureSizes(
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureSizes* outSizes
    ) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE for platforms
    // without ray tracing support.
    virtual SLANG_NO_THROW Result SLANG_MCALL createAccelerationStructure(
        const AccelerationStructureDesc& desc,
        IAccelerationStructure** outAccelerationStructure
    ) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE for platforms
    // without ray tracing support.
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createShaderTable(const ShaderTableDesc& desc, IShaderTable** outTable) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL createFence(const FenceDesc& desc, IFence** outFence) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFences(uint32_t fenceCount, IFence** fences, const uint64_t* fenceValues, bool waitForAll, uint64_t timeout)
        override;

    // Default implementation uses encoder.copyTextureToBuffer to copy to the read-back heap
    virtual SLANG_NO_THROW Result SLANG_MCALL
    readTexture(ITexture* texture, uint32_t layer, uint32_t mip, const SubresourceLayout& layout, void* outData)
        override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    readTexture(ITexture* texture, uint32_t layer, uint32_t mip, ISlangBlob** outBlob, SubresourceLayout* outLayout)
        override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    readBuffer(IBuffer* buffer, Offset offset, Size size, ISlangBlob** outBlob) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL
    getTextureAllocationInfo(const TextureDesc& desc, Size* outSize, Size* outAlignment) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureRowAlignment(Format format, size_t* outAlignment) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL createSurface(WindowHandle windowHandle, ISurface** outSurface) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL
    getCooperativeVectorProperties(CooperativeVectorProperties* properties, uint32_t* propertyCount) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL
    convertCooperativeVectorMatrix(const ConvertCooperativeVectorMatrixDesc* descs, uint32_t descCount) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    getShaderCacheStats(size_t* outCacheHitCount, size_t* outCacheMissCount, size_t* outCacheSize) override;

    Result getEntryPointCodeFromShaderCache(
        slang::IComponentType* program,
        SlangInt entryPointIndex,
        SlangInt targetIndex,
        slang::IBlob** outCode,
        slang::IBlob** outDiagnostics = nullptr
    );

    Result getShaderObjectLayout(
        slang::ISession* session,
        slang::TypeReflection* type,
        ShaderObjectContainerType container,
        ShaderObjectLayout** outLayout
    );

    Result getShaderObjectLayout(
        slang::ISession* session,
        slang::TypeLayoutReflection* typeLayout,
        ShaderObjectLayout** outLayout
    );


public:
    inline void handleMessage(DebugMessageType type, DebugMessageSource source, const char* message)
    {
        m_debugCallback->handleMessage(type, source, message);
    }

    inline void warning(const char* message)
    {
        handleMessage(DebugMessageType::Warning, DebugMessageSource::Layer, message);
    }

    Result createShaderObject(ShaderObjectLayout* layout, ShaderObject** outObject);
    Result createRootShaderObject(ShaderProgram* program, RootShaderObject** outObject);

    Result getSpecializedProgram(
        ShaderProgram* program,
        const ExtendedShaderObjectTypeList& specializationArgs,
        ShaderProgram** outSpecializedProgram
    );

    Result specializeProgram(
        ShaderProgram* program,
        const ExtendedShaderObjectTypeList& specializationArgs,
        ShaderProgram** outSpecializedProgram
    );

    Result getConcretePipeline(
        Pipeline* pipeline,
        ExtendedShaderObjectTypeList* specializationArgs,
        Pipeline*& outPipeline
    );

    virtual Result createShaderObjectLayout(
        slang::ISession* session,
        slang::TypeLayoutReflection* typeLayout,
        ShaderObjectLayout** outLayout
    ) = 0;

    virtual Result createRootShaderObjectLayout(
        slang::IComponentType* program,
        slang::ProgramLayout* programLayout,
        ShaderObjectLayout** outLayout
    ) = 0;

    virtual void customizeShaderObject(ShaderObject* shaderObject) { SLANG_UNUSED(shaderObject); }

    virtual Result createRenderPipeline2(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline);
    virtual Result createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline);
    virtual Result createRayTracingPipeline2(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline);

protected:
    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const DeviceDesc& desc);

    void addFeature(Feature feature);
    void addCapability(Capability capability);

protected:
    std::array<bool, size_t(Feature::_Count)> m_featureSet;
    std::array<bool, size_t(Capability::_Count)> m_capabilitySet;
    std::array<FormatSupport, size_t(Format::_Count)> m_formatSupport;
    std::vector<CooperativeVectorProperties> m_cooperativeVectorProperties;

public:
    DeviceInfo m_info;

    SlangContext m_slangContext;
    ShaderCache m_shaderCache;
    StagingHeap m_uploadHeap;
    StagingHeap m_readbackHeap;

    ComPtr<IPersistentShaderCache> m_persistentShaderCache;
    size_t m_shaderCacheHits = 0;
    size_t m_shaderCacheMisses = 0;

    std::map<slang::TypeLayoutReflection*, RefPtr<ShaderObjectLayout>> m_shaderObjectLayoutCache;
    ComPtr<IPipelineCreationAPIDispatcher> m_pipelineCreationAPIDispatcher;

    IDebugCallback* m_debugCallback = nullptr;
};

} // namespace rhi
