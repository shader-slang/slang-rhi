#pragma once

#include "d3d12-command.h"
#include "d3d12-texture.h"
#include "d3d12-bindless-descriptor-set.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>

namespace rhi::d3d12 {

// Define function pointer types for PIX library.
typedef HRESULT(WINAPI* PFN_BeginEventOnCommandList)(
    ID3D12GraphicsCommandList* commandList,
    UINT64 color,
    PCSTR formatString
);
typedef HRESULT(WINAPI* PFN_EndEventOnCommandList)(ID3D12GraphicsCommandList* commandList);
typedef HRESULT(WINAPI* PFN_SetMarkerOnCommandList)(
    ID3D12GraphicsCommandList* commandList,
    UINT64 color,
    PCSTR formatString
);

struct D3D12DeviceInfo
{
    void clear()
    {
        m_dxgiFactory.setNull();
        m_device.setNull();
        m_adapter.setNull();
        m_desc = {};
        m_desc1 = {};
        m_isWarp = false;
        m_isSoftware = false;
    }

    bool m_isWarp;
    bool m_isSoftware;
    ComPtr<IDXGIFactory> m_dxgiFactory;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12Device5> m_device5;
    ComPtr<IDXGIAdapter> m_adapter;
    DXGI_ADAPTER_DESC m_desc;
    DXGI_ADAPTER_DESC1 m_desc1;
};

class DeviceImpl : public Device
{
public:
    D3D12DeviceExtendedDesc m_extendedDesc;

    std::string m_adapterName;

    ComPtr<ID3D12Debug> m_dxDebug;

    static const bool g_isAftermathEnabled;

    D3D12DeviceInfo m_deviceInfo;
    ID3D12Device* m_device = nullptr;
    ID3D12Device5* m_device5 = nullptr;

    RefPtr<CommandQueueImpl> m_queue;

    RefPtr<CPUDescriptorHeap> m_cpuCbvSrvUavHeap;
    RefPtr<CPUDescriptorHeap> m_cpuRtvHeap;
    RefPtr<CPUDescriptorHeap> m_cpuDsvHeap;
    RefPtr<CPUDescriptorHeap> m_cpuSamplerHeap;

    RefPtr<GPUDescriptorHeap> m_gpuCbvSrvUavHeap;
    RefPtr<GPUDescriptorHeap> m_gpuSamplerHeap;

    // Dll entry points
    PFN_D3D12_GET_DEBUG_INTERFACE m_D3D12GetDebugInterface = nullptr;
    PFN_D3D12_CREATE_DEVICE m_D3D12CreateDevice = nullptr;
    PFN_D3D12_SERIALIZE_ROOT_SIGNATURE m_D3D12SerializeRootSignature = nullptr;
    PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE m_D3D12SerializeVersionedRootSignature = nullptr;
    PFN_BeginEventOnCommandList m_BeginEventOnCommandList = nullptr;
    PFN_EndEventOnCommandList m_EndEventOnCommandList = nullptr;
    PFN_SetMarkerOnCommandList m_SetMarkerOnCommandList = nullptr;

#if SLANG_RHI_ENABLE_NVAPI
    NVAPIShaderExtension m_nvapiShaderExtension;
#if SLANG_RHI_DXR
    void* m_raytracingValidationHandle = nullptr;
#endif
#endif

    // Command signatures required for indirect draws. These indicate the format of the indirect
    // as well as the command type to be used (DrawInstanced and DrawIndexedInstanced, in this
    // case).
    ComPtr<ID3D12CommandSignature> drawIndirectCmdSignature;
    ComPtr<ID3D12CommandSignature> drawIndexedIndirectCmdSignature;
    ComPtr<ID3D12CommandSignature> dispatchIndirectCmdSignature;

public:
    using Device::readBuffer;

    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const DeviceDesc& desc) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getQueue(QueueType type, ICommandQueue** outQueue) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSurface(WindowHandle windowHandle, ISurface** outSurface) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    getTextureAllocationInfo(const TextureDesc& desc, Size* outSize, Size* outAlignment) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureRowAlignment(Format format, Size* outAlignment) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTexture(const TextureDesc& desc, const SubresourceData* initData, ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTextureFromNativeHandle(NativeHandle handle, const TextureDesc& desc, ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBuffer(const BufferDesc& desc, const void* initData, IBuffer** outBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL unmapBuffer(IBuffer* buffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(const SamplerDesc& desc, ISampler** outSampler) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createInputLayout(const InputLayoutDesc& desc, IInputLayout** outLayout) override;

    virtual Result createShaderObjectLayout(
        slang::ISession* session,
        slang::TypeLayoutReflection* typeLayout,
        ShaderObjectLayout** outLayout
    ) override;

    virtual Result createRootShaderObjectLayout(
        slang::IComponentType* program,
        slang::ProgramLayout* programLayout,
        ShaderObjectLayout** outLayout
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createShaderTable(const ShaderTableDesc& desc, IShaderTable** outShaderTable) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderProgram(
        const ShaderProgramDesc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnostics
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRenderPipeline2(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRayTracingPipeline2(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createQueryPool(const QueryPoolDesc& desc, IQueryPool** outState) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createFence(const FenceDesc& desc, IFence** outFence) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFences(uint32_t fenceCount, IFence** fences, const uint64_t* fenceValues, bool waitForAll, uint64_t timeout)
        override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    readBuffer(IBuffer* buffer, Offset offset, Size size, void* outData) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeDeviceHandles(DeviceNativeHandles* outHandles) override;

    ~DeviceImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL getAccelerationStructureSizes(
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureSizes* outSizes
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createAccelerationStructure(
        const AccelerationStructureDesc& desc,
        IAccelerationStructure** outAccelerationStructure
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    getCooperativeVectorProperties(CooperativeVectorProperties* properties, uint32_t* propertyCount) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    convertCooperativeVectorMatrix(const ConvertCooperativeVectorMatrixDesc* descs, uint32_t descCount) override;

public:
    static void* loadProc(SharedLibraryHandle module, const char* name);

    Result createBuffer(
        const D3D12_RESOURCE_DESC& resourceDesc,
        const void* srcData,
        Size srcDataSize,
        D3D12_RESOURCE_STATES finalState,
        D3D12Resource& resourceOut,
        bool isShared,
        MemoryType access = MemoryType::DeviceLocal
    );

    Result _createDevice(
        DeviceCheckFlags deviceCheckFlags,
        const AdapterLUID* adapterLUID,
        D3D_FEATURE_LEVEL featureLevel,
        D3D12DeviceInfo& outDeviceInfo
    );

    struct
    {
        std::mutex mutex;
        ComPtr<ID3D12CommandAllocator> commandAllocator;
        ComPtr<ID3D12GraphicsCommandList> commandList;
    } m_immediateCommandList;

    ID3D12GraphicsCommandList* beginImmediateCommandList();
    void endImmediateCommandList();

    void flushValidationMessages();

    RefPtr<BindlessDescriptorSet> m_bindlessDescriptorSet;

    using NullDescriptorKey = std::pair<slang::BindingType, SlangResourceShape>;
    std::map<NullDescriptorKey, CPUDescriptorAllocation> m_nullDescriptors;
    CPUDescriptorAllocation m_nullSamplerDescriptor;
    std::mutex m_nullDescriptorsMutex;
    D3D12_CPU_DESCRIPTOR_HANDLE getNullDescriptor(slang::BindingType bindingType, SlangResourceShape resourceShape);
    D3D12_CPU_DESCRIPTOR_HANDLE getNullSamplerDescriptor();

private:
    void processExperimentalFeaturesDesc(SharedLibraryHandle d3dModule, const void* desc);
};

} // namespace rhi::d3d12
