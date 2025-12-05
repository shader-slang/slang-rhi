#pragma once

#include <slang-rhi.h>

#include "slang-context.h"

#include "core/common.h"
#include "core/short_vector.h"

#include "staging-heap.h"

#include "rhi-shared-fwd.h"

#include <atomic>
#include <map>
#include <unordered_map>

namespace rhi {

// Forward declarations
class Heap;

namespace testing {
// Debug option for tests to turn off state tracking (so we can effectively test explicit barriers)
extern bool gDebugDisableStateTracking;
} // namespace testing

// Base class for adapters.
// We specifically don't use ComObject as we don't want ref counting.
// Adapters are lazily created and stored in static vectors and only released on program exit.
class Adapter : public IAdapter
{
public:
    virtual ~Adapter() {}

    virtual SLANG_NO_THROW const AdapterInfo& SLANG_MCALL getInfo() const override { return m_info; }

    bool isNVIDIA() const { return m_info.vendorID == 0x10DE || m_info.deviceType == DeviceType::CUDA; }

public:
    AdapterInfo m_info;
    /// True if this is the default adapter to use during automatic adapter selection.
    bool m_isDefault = false;
};

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

    RefPtr<Pipeline> getSpecializedPipeline(PipelineKey programKey);

    void addSpecializedPipeline(PipelineKey key, RefPtr<Pipeline> specializedPipeline);

    void free();

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
    virtual SLANG_NO_THROW void SLANG_MCALL handleMessage(
        DebugMessageType type,
        DebugMessageSource source,
        const char* message
    ) override
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
    virtual SLANG_NO_THROW Result SLANG_MCALL getCapabilities(
        uint32_t* outCapabilityCount,
        Capability* outCapabilities
    ) override;
    virtual SLANG_NO_THROW bool SLANG_MCALL hasCapability(Capability capability) override;
    virtual SLANG_NO_THROW bool SLANG_MCALL hasCapability(const char* capability) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getFormatSupport(Format format, FormatSupport* outFormatSupport) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSlangSession(slang::ISession** outSlangSession) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(const SlangUUID& uuid, void** outObject) override;
    IDevice* getInterface(const Guid& guid);

    virtual SLANG_NO_THROW Result SLANG_MCALL createTextureFromNativeHandle(
        NativeHandle handle,
        const TextureDesc& desc,
        ITexture** outTexture
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createTextureFromSharedHandle(
        NativeHandle handle,
        const TextureDesc& desc,
        const Size size,
        ITexture** outTexture
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createBufferFromNativeHandle(
        NativeHandle handle,
        const BufferDesc& desc,
        IBuffer** outBuffer
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createBufferFromSharedHandle(
        NativeHandle handle,
        const BufferDesc& desc,
        IBuffer** outBuffer
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createInputLayout(
        const InputLayoutDesc& desc,
        IInputLayout** outLayout
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createRenderPipeline(
        const RenderPipelineDesc& desc,
        IRenderPipeline** outPipeline
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createComputePipeline(
        const ComputePipelineDesc& desc,
        IComputePipeline** outPipeline
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createRayTracingPipeline(
        const RayTracingPipelineDesc& desc,
        IRayTracingPipeline** outPipeline
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getCompilationReportList(ISlangBlob** outReportListBlob) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderObject(
        slang::ISession* session,
        slang::TypeReflection* type,
        ShaderObjectContainerType containerType,
        IShaderObject** outObject
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderObjectFromTypeLayout(
        slang::TypeLayoutReflection* typeLayout,
        IShaderObject** outObject
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createRootShaderObject(
        IShaderProgram* program,
        IShaderObject** outObject
    ) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE for platforms
    // without ray tracing support.
    virtual SLANG_NO_THROW Result SLANG_MCALL getAccelerationStructureSizes(
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureSizes* outSizes
    ) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE for platforms
    // without cluster acceleration support.
    virtual SLANG_NO_THROW Result SLANG_MCALL getClusterOperationSizes(
        const ClusterOperationParams& params,
        ClusterOperationSizes* outSizes
    ) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE for platforms
    // without ray tracing support.
    virtual SLANG_NO_THROW Result SLANG_MCALL createAccelerationStructure(
        const AccelerationStructureDesc& desc,
        IAccelerationStructure** outAccelerationStructure
    ) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE for platforms
    // without ray tracing support.
    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderTable(
        const ShaderTableDesc& desc,
        IShaderTable** outTable
    ) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL createFence(const FenceDesc& desc, IFence** outFence) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL waitForFences(
        uint32_t fenceCount,
        IFence** fences,
        const uint64_t* fenceValues,
        bool waitForAll,
        uint64_t timeout
    ) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL createHeap(const HeapDesc& desc, IHeap** outHeap) override;

    // Default implementation uses encoder.copyTextureToBuffer to copy to the read-back heap
    virtual SLANG_NO_THROW Result SLANG_MCALL readTexture(
        ITexture* texture,
        uint32_t layer,
        uint32_t mip,
        const SubresourceLayout& layout,
        void* outData
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL readTexture(
        ITexture* texture,
        uint32_t layer,
        uint32_t mip,
        ISlangBlob** outBlob,
        SubresourceLayout* outLayout
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL readBuffer(
        IBuffer* buffer,
        Offset offset,
        Size size,
        ISlangBlob** outBlob
    ) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureAllocationInfo(
        const TextureDesc& desc,
        Size* outSize,
        Size* outAlignment
    ) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureRowAlignment(Format format, size_t* outAlignment) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL createSurface(WindowHandle windowHandle, ISurface** outSurface) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL getCooperativeVectorProperties(
        CooperativeVectorProperties* properties,
        uint32_t* propertiesCount
    ) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL getCooperativeVectorMatrixSize(
        uint32_t rowCount,
        uint32_t colCount,
        CooperativeVectorComponentType componentType,
        CooperativeVectorMatrixLayout layout,
        size_t rowColumnStride,
        size_t* outSize
    ) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL convertCooperativeVectorMatrix(
        void* dstBuffer,
        size_t dstBufferSize,
        const CooperativeVectorMatrixDesc* dstDescs,
        const void* srcBuffer,
        size_t srcBufferSize,
        const CooperativeVectorMatrixDesc* srcDescs,
        uint32_t matrixCount
    ) override;

    // Provides a default implementation that reports heaps from m_globalHeaps.
    virtual SLANG_NO_THROW Result SLANG_MCALL reportHeaps(HeapReport* heapReports, uint32_t* heapCount) override;

    // Flush all global heaps managed by this device
    Result flushHeaps();

    Result getEntryPointCodeFromShaderCache(
        ShaderProgram* program,
        slang::IComponentType* componentType,
        const char* entryPointName,
        uint32_t entryPointIndex,
        uint32_t targetIndex,
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

    void printMessage(DebugMessageType type, DebugMessageSource source, const char* message, ...);
    void printInfo(const char* message, ...);
    void printWarning(const char* message, ...);
    void printError(const char* message, ...);

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
    std::vector<Capability> getCapabilities();

protected:
    std::array<bool, size_t(Feature::_Count)> m_featureSet;
    std::array<bool, size_t(Capability::_Count)> m_capabilitySet;
    std::array<FormatSupport, size_t(Format::_Count)> m_formatSupport;
    std::vector<CooperativeVectorProperties> m_cooperativeVectorProperties;

public:
    DeviceInfo m_info;

    SlangContext m_slangContext;
    ShaderCache m_shaderCache;

    std::atomic<uint64_t> m_nextShaderProgramID = 0;
    RefPtr<ShaderCompilationReporter> m_shaderCompilationReporter;

    StagingHeap m_uploadHeap;
    StagingHeap m_readbackHeap;

    ComPtr<IPersistentCache> m_persistentShaderCache;
    ComPtr<IPersistentCache> m_persistentPipelineCache;

    std::map<slang::TypeLayoutReflection*, RefPtr<ShaderObjectLayout>> m_shaderObjectLayoutCache;

    // List of heaps managed by this device. DeviceImpl is expected
    // to hold references to them.
    std::vector<Heap*> m_globalHeaps;

    IDebugCallback* m_debugCallback = nullptr;
};

/// Mark the default adapter in the list, preferring the first discrete adapter.
template<typename T>
void markDefaultAdapter(std::vector<T>& adapters)
{
    if (!adapters.empty())
    {
        size_t best = 0;
        for (size_t i = 0; i < adapters.size(); i++)
        {
            if (adapters[i].m_info.adapterType == AdapterType::Discrete)
            {
                best = i;
                break;
            }
        }
        adapters[best].m_isDefault = true;
    }
}

template<typename T>
Result selectAdapter(Device* device, std::vector<T>& adapters, const DeviceDesc& desc, T*& outAdapter)
{
    if (adapters.empty())
    {
        device->printError("No adapters found\n");
        return SLANG_FAIL;
    }
    if (desc.adapter)
    {
        // Select provided adapter, check it is valid.
        bool valid = false;
        for (auto& adapter : adapters)
        {
            if (&adapter == desc.adapter)
            {
                valid = true;
                break;
            }
        }
        if (!valid)
        {
            device->printError("Invalid adapter\n");
            return SLANG_FAIL;
        }
        outAdapter = checked_cast<T*>(desc.adapter);
    }
    else if (desc.adapterLUID)
    {
        // Select adapter based on LUID.
        bool found = false;
        for (auto& adapter : adapters)
        {
            if (adapter.getInfo().luid == *desc.adapterLUID)
            {
                outAdapter = &adapter;
                found = true;
                break;
            }
        }
        if (!found)
        {
            device->printError("Invalid adapter LUID\n");
            return SLANG_FAIL;
        }
    }
    else
    {
        // Select the default adapter or the first one if no default is available.
        outAdapter = &adapters[0];
        for (auto& adapter : adapters)
        {
            if (adapter.m_isDefault)
            {
                outAdapter = &adapter;
                break;
            }
        }
    }
    return SLANG_OK;
}


} // namespace rhi
