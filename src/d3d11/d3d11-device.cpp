#define _CRT_SECURE_NO_WARNINGS
#include "d3d11-device.h"
#include "d3d11-buffer.h"
#include "d3d11-utils.h"
#include "d3d11-query.h"
#include "d3d11-sampler.h"
#include "d3d11-shader-object-layout.h"
#include "d3d11-shader-object.h"
#include "d3d11-shader-program.h"
#include "d3d11-surface.h"
#include "d3d11-texture.h"
#include "d3d11-input-layout.h"
#include "d3d11-command.h"

#include "core/string.h"

#if SLANG_RHI_ENABLE_AFTERMATH
#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_Defines.h"
#include "GFSDK_Aftermath_GpuCrashDump.h"
#endif

namespace rhi::d3d11 {

inline Result getAdaptersImpl(std::vector<AdapterImpl>& outAdapters)
{
    std::vector<ComPtr<IDXGIAdapter>> dxgiAdapters;
    SLANG_RETURN_ON_FAIL(enumAdapters(dxgiAdapters));

    for (const auto& dxgiAdapter : dxgiAdapters)
    {
        AdapterInfo info = getAdapterInfo(dxgiAdapter);
        info.deviceType = DeviceType::D3D11;

        AdapterImpl adapter;
        adapter.m_info = info;
        adapter.m_dxgiAdapter = dxgiAdapter;
        outAdapters.push_back(adapter);
    }

    // Mark default adapter (prefer discrete if available).
    markDefaultAdapter(outAdapters);

    return SLANG_OK;
}

std::vector<AdapterImpl>& getAdapters()
{
    static std::vector<AdapterImpl> adapters;
    static Result initResult = getAdaptersImpl(adapters);
    SLANG_UNUSED(initResult);
    return adapters;
}

DeviceImpl::DeviceImpl() {}

DeviceImpl::~DeviceImpl() {}

Result DeviceImpl::initialize(const DeviceDesc& desc)
{
    SLANG_RETURN_ON_FAIL(Device::initialize(desc));

    // Rather than statically link against D3D, we load it dynamically.
    SharedLibraryHandle d3dModule;
#if SLANG_WINDOWS_FAMILY
    const char* const libName = "d3d11";
#else
    const char* const libName = "libdxvk_d3d11.so";
#endif
    if (SLANG_FAILED(loadSharedLibrary(libName, d3dModule)))
    {
        printError("Failed to load '%s'\n", libName);
        return SLANG_FAIL;
    }

    PFN_D3D11_CREATE_DEVICE D3D11CreateDevice_ =
        (PFN_D3D11_CREATE_DEVICE)findSymbolAddressByName(d3dModule, "D3D11CreateDevice");
    if (!D3D11CreateDevice_)
    {
        printError("Failed to load symbol 'D3D11CreateDevice'\n");
        return SLANG_FAIL;
    }

    m_dxgiFactory = getDXGIFactory();

    AdapterImpl* adapter = nullptr;
    SLANG_RETURN_ON_FAIL(selectAdapter(this, getAdapters(), desc, adapter));
    m_dxgiAdapter = adapter->m_dxgiAdapter;

    // We will ask for the highest feature level that can be supported.
    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1,
    };

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL(0);

    // When creating the D3D11 device we need to consider:
    // - Creation may fail if debug layer is requested but not available on the system.
    // - Creation may fail if feature level 11_1 is requested on a system that does not support it.
    // To handle this we try a few combinations until one works or we run out of options.
    // The order of flags is important, as we want to try the most specific options first.

    enum CreateFlag
    {
        UseDebug = 1,
        Use11_1 = 2
    };
    int createFlags[] = {UseDebug | Use11_1, UseDebug, Use11_1, 0};
    int usedCreateFlags = 0;

    Result result = SLANG_FAIL;
    for (uint32_t i = isDebugLayersEnabled() ? 0 : 2; i < SLANG_COUNT_OF(createFlags); i++)
    {
        usedCreateFlags = createFlags[i];
        bool useDebug = (usedCreateFlags & UseDebug) != 0;
        bool use11_1 = (usedCreateFlags & Use11_1) != 0;
        result = D3D11CreateDevice_(
            m_dxgiAdapter,
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            useDebug ? D3D11_CREATE_DEVICE_DEBUG : 0,
            use11_1 ? featureLevels : featureLevels + 1,
            use11_1 ? SLANG_COUNT_OF(featureLevels) : SLANG_COUNT_OF(featureLevels) - 1,
            D3D11_SDK_VERSION,
            m_device.writeRef(),
            &featureLevel,
            m_immediateContext.writeRef()
        );
        if (SUCCEEDED(result))
            break;
    }
    if (FAILED(result))
    {
        printError("D3D11CreateDevice failed: %08x\n", result);
        return SLANG_FAIL;
    }
    if (isDebugLayersEnabled() && (usedCreateFlags & UseDebug) == 0)
    {
        printWarning("Debug layer requested but not available.\n");
    }

#if SLANG_RHI_ENABLE_AFTERMATH
    if (desc.enableAftermath && adapter->isNVIDIA())
    {
        // Initialize Nsight Aftermath for this device.
        // This combination of flags is not necessarily appropriate for real world usage
        const uint32_t aftermathFlags =
            GFSDK_Aftermath_FeatureFlags_EnableMarkers | GFSDK_Aftermath_FeatureFlags_CallStackCapturing |
            GFSDK_Aftermath_FeatureFlags_EnableResourceTracking | GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo |
            GFSDK_Aftermath_FeatureFlags_EnableShaderErrorReporting;

        auto initResult = GFSDK_Aftermath_DX11_Initialize(GFSDK_Aftermath_Version_API, aftermathFlags, m_device);

        if (initResult != GFSDK_Aftermath_Result_Success)
        {
            printWarning("Failed to initialize aftermath: %d\n", int(initResult));
        }
    }
#endif

    SLANG_RHI_ASSERT(m_device && m_immediateContext);
    SLANG_RETURN_ON_FAIL(m_immediateContext->QueryInterface(m_immediateContext1.writeRef()));

    // Initialize device info
    {
        m_info.deviceType = DeviceType::D3D11;
        m_info.apiName = "D3D11";
    }

    // Query adapter name & LUID
    {
        DXGI_ADAPTER_DESC adapterDesc;
        m_dxgiAdapter->GetDesc(&adapterDesc);
        m_adapterName = string::from_wstring(adapterDesc.Description);
        m_info.adapterName = m_adapterName.data();
        m_info.adapterLUID = getAdapterLUID(adapterDesc.AdapterLuid);
    }

    // Query timestamp frequency
    {
        D3D11_QUERY_DESC disjointQueryDesc = {};
        disjointQueryDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        SLANG_RETURN_ON_FAIL(m_device->CreateQuery(&disjointQueryDesc, m_disjointQuery.writeRef()));
        m_immediateContext->Begin(m_disjointQuery);
        m_immediateContext->End(m_disjointQuery);
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData = {};
        m_immediateContext->Flush();
        m_immediateContext->GetData(m_disjointQuery, &disjointData, sizeof(disjointData), 0);
        m_info.timestampFrequency = disjointData.Frequency;
    }

    // Query device limits
    {
        uint32_t maxTextureDimensionUV = 2048;
        if (featureLevel >= D3D_FEATURE_LEVEL_9_3)
            maxTextureDimensionUV = 4096;
        if (featureLevel >= D3D_FEATURE_LEVEL_10_0)
            maxTextureDimensionUV = 8192;
        if (featureLevel >= D3D_FEATURE_LEVEL_11_0)
            maxTextureDimensionUV = 16384;

        uint32_t maxTextureDimensionW = 256;
        if (featureLevel >= D3D_FEATURE_LEVEL_10_0)
            maxTextureDimensionW = 2048;

        uint32_t maxTextureDimensionCube = 512;
        if (featureLevel >= D3D_FEATURE_LEVEL_9_3)
            maxTextureDimensionCube = maxTextureDimensionUV;

        uint32_t maxInputElements = 16;
        if (featureLevel >= D3D_FEATURE_LEVEL_10_1)
            maxInputElements = 32;

        // uint32_t maxColorAttachments = 4;
        // if (featureLevel >= D3D_FEATURE_LEVEL_10_1)
        //     maxColorAttachments = 8;

        uint32_t maxComputeThreadGroupSizeXY = 0;
        uint32_t maxComputeThreadGroupSizeZ = 0;
        uint32_t maxComputeDispatchThreadGroupsZ = 0;
        if (featureLevel >= D3D_FEATURE_LEVEL_10_0)
        {
            maxComputeThreadGroupSizeXY = D3D11_CS_4_X_THREAD_GROUP_MAX_X;
            maxComputeThreadGroupSizeZ = 1;
            maxComputeDispatchThreadGroupsZ = 1;
        }
        if (featureLevel >= D3D_FEATURE_LEVEL_11_0)
        {
            maxComputeThreadGroupSizeXY = D3D11_CS_THREAD_GROUP_MAX_X;
            maxComputeThreadGroupSizeZ = D3D11_CS_THREAD_GROUP_MAX_Z;
            maxComputeDispatchThreadGroupsZ = D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
        }

        DeviceLimits limits = {};
        limits.maxBufferSize = 0x80000000ull; // Assume 2GB
        limits.maxTextureDimension1D = maxTextureDimensionUV;
        limits.maxTextureDimension2D = maxTextureDimensionUV;
        limits.maxTextureDimension3D = maxTextureDimensionW;
        limits.maxTextureDimensionCube = maxTextureDimensionCube;
        limits.maxTextureLayers = maxTextureDimensionCube;

        limits.maxVertexInputElements = maxInputElements;
        limits.maxVertexInputElementOffset = 256; // TODO
        limits.maxVertexStreams = D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
        limits.maxVertexStreamStride = D3D11_REQ_MULTI_ELEMENT_STRUCTURE_SIZE_IN_BYTES;

        limits.maxComputeThreadsPerGroup = D3D11_CS_THREAD_GROUP_MAX_THREADS_PER_GROUP;
        limits.maxComputeThreadGroupSize[0] = maxComputeThreadGroupSizeXY;
        limits.maxComputeThreadGroupSize[1] = maxComputeThreadGroupSizeXY;
        limits.maxComputeThreadGroupSize[2] = maxComputeThreadGroupSizeZ;
        limits.maxComputeDispatchThreadGroups[0] = D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
        limits.maxComputeDispatchThreadGroups[1] = D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
        limits.maxComputeDispatchThreadGroups[2] = maxComputeDispatchThreadGroupsZ;

        limits.maxViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        limits.maxViewportDimensions[0] = D3D11_VIEWPORT_BOUNDS_MAX;
        limits.maxViewportDimensions[1] = D3D11_VIEWPORT_BOUNDS_MAX;
        limits.maxFramebufferDimensions[0] = 4096; // TODO
        limits.maxFramebufferDimensions[1] = 4096; // TODO
        limits.maxFramebufferDimensions[2] = 1;

        limits.maxShaderVisibleSamplers = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;

        m_info.limits = limits;
    }

    // Initialize features & capabilities
    bool isSoftwareDevice = adapter->m_info.adapterType == AdapterType::Software;
    addFeature(isSoftwareDevice ? Feature::SoftwareDevice : Feature::HardwareDevice);
    addFeature(Feature::ParameterBlock);
    addFeature(Feature::Surface);
    addFeature(Feature::Rasterization);
    addFeature(Feature::CustomBorderColor);
    if (m_info.timestampFrequency > 0)
    {
        addFeature(Feature::TimestampQuery);
    }

    addCapability(Capability::hlsl);

// Initialize NVAPI
#if SLANG_RHI_ENABLE_NVAPI
    {
        if (adapter->isNVIDIA() && SLANG_SUCCEEDED(NVAPIUtil::initialize()))
        {
            m_nvapiShaderExtension = NVAPIShaderExtension{desc.nvapiExtUavSlot, desc.nvapiExtRegisterSpace};
            if (m_nvapiShaderExtension)
            {
                addCapability(Capability::hlsl_nvapi);

                if (isSupportedNVAPIOp(m_device, NV_EXTN_OP_UINT64_ATOMIC))
                {
                    addFeature(Feature::AtomicInt64);
                }
                if (isSupportedNVAPIOp(m_device, NV_EXTN_OP_FP16_ATOMIC))
                {
                    addFeature(Feature::AtomicHalf);
                }
                if (isSupportedNVAPIOp(m_device, NV_EXTN_OP_FP32_ATOMIC))
                {
                    addFeature(Feature::AtomicFloat);
                }
                if (isSupportedNVAPIOp(m_device, NV_EXTN_OP_GET_SPECIAL))
                {
                    addFeature(Feature::RealtimeClock);
                }
            }
        }
    }
#endif // SLANG_RHI_ENABLE_NVAPI

    // Check double precision support
    {
        D3D11_FEATURE_DATA_DOUBLES doublePrecisionFeature = {};
        if (SUCCEEDED(m_device->CheckFeatureSupport(
                D3D11_FEATURE_DOUBLES,
                &doublePrecisionFeature,
                sizeof(doublePrecisionFeature)
            )) &&
            doublePrecisionFeature.DoublePrecisionFloatShaderOps)
        {
            addFeature(Feature::Double);
        }
    }

    // Initialize format support table
    for (size_t formatIndex = 0; formatIndex < size_t(Format::_Count); ++formatIndex)
    {
        Format format = Format(formatIndex);
        const FormatMapping& formatMapping = getFormatMapping(format);
        FormatSupport formatSupport = FormatSupport::None;

#define UPDATE_FLAGS(d3dFlags, formatSupportFlags)                                                                     \
    formatSupport |= (flags & d3dFlags) ? formatSupportFlags : FormatSupport::None;

        D3D11_FEATURE_DATA_FORMAT_SUPPORT d3dFormatSupport = {formatMapping.srvFormat};
        if (SLANG_SUCCEEDED(
                m_device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT, &d3dFormatSupport, sizeof(d3dFormatSupport))
            ))
        {
            UINT flags = d3dFormatSupport.OutFormatSupport;
            UPDATE_FLAGS(D3D11_FORMAT_SUPPORT_BUFFER, FormatSupport::Buffer);
            UPDATE_FLAGS(D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER, FormatSupport::VertexBuffer);
            UPDATE_FLAGS(D3D11_FORMAT_SUPPORT_IA_INDEX_BUFFER, FormatSupport::IndexBuffer);
            UPDATE_FLAGS(D3D11_FORMAT_SUPPORT_TEXTURE1D, FormatSupport::Texture);
            UPDATE_FLAGS(D3D11_FORMAT_SUPPORT_TEXTURE2D, FormatSupport::Texture);
            UPDATE_FLAGS(D3D11_FORMAT_SUPPORT_TEXTURE3D, FormatSupport::Texture);
            UPDATE_FLAGS(D3D11_FORMAT_SUPPORT_TEXTURECUBE, FormatSupport::Texture);
            UPDATE_FLAGS(D3D11_FORMAT_SUPPORT_SHADER_LOAD, FormatSupport::ShaderLoad);
            UPDATE_FLAGS(D3D11_FORMAT_SUPPORT_SHADER_SAMPLE, FormatSupport::ShaderSample);
            UPDATE_FLAGS(D3D11_FORMAT_SUPPORT_RENDER_TARGET, FormatSupport::RenderTarget);
            UPDATE_FLAGS(D3D11_FORMAT_SUPPORT_BLENDABLE, FormatSupport::Blendable);
            UPDATE_FLAGS(D3D11_FORMAT_SUPPORT_DEPTH_STENCIL, FormatSupport::DepthStencil);
            UPDATE_FLAGS(D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE, FormatSupport::Resolvable);
            UPDATE_FLAGS(D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET, FormatSupport::Multisampling);
            UPDATE_FLAGS(
                D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW,
                FormatSupport::ShaderUavLoad | FormatSupport::ShaderUavStore
            );
        }
        D3D11_FEATURE_DATA_FORMAT_SUPPORT2 d3dFormatSupport2 = {formatMapping.srvFormat};
        if (SLANG_SUCCEEDED(m_device->CheckFeatureSupport(
                D3D11_FEATURE_FORMAT_SUPPORT2,
                &d3dFormatSupport2,
                sizeof(d3dFormatSupport2)
            )))
        {
            UINT flags = d3dFormatSupport2.OutFormatSupport2;
            if (is_set(formatSupport, FormatSupport::ShaderUavStore))
            {
                UPDATE_FLAGS(
                    (D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_ADD | D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS |
                     D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE |
                     D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE | D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX |
                     D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX),
                    FormatSupport::ShaderAtomic
                );
            }
        }

#undef UPDATE_FLAGS

        if (formatSupport != FormatSupport::None)
        {
            formatSupport |= FormatSupport::CopySource | FormatSupport::CopyDestination;
        }

        m_formatSupport[formatIndex] = formatSupport;
    }

    // Initialize slang context
    SLANG_RETURN_ON_FAIL(
        m_slangContext
            .initialize(desc.slang, SLANG_DXBC, "sm_5_0", std::array{slang::PreprocessorMacroDesc{"__D3D11__", "1"}})
    );

    m_queue = new CommandQueueImpl(this, QueueType::Graphics);
    m_queue->setInternalReferenceCount(1);

    return SLANG_OK;
}

Result DeviceImpl::readTexture(
    ITexture* texture,
    uint32_t layer,
    uint32_t mip,
    const SubresourceLayout& layout,
    void* outData
)
{
    auto textureImpl = checked_cast<TextureImpl*>(texture);

    // Don't bother supporting MSAA for right now
    if (textureImpl->m_desc.sampleCount > 1)
    {
        fprintf(stderr, "ERROR: cannot capture multi-sample texture\n");
        return E_INVALIDARG;
    }

    // Get texture descriptor.
    TextureDesc desc = textureImpl->getDesc();

    // This pointer exists at root scope to ensure that if a temp texture
    // needs to be made, it is kept alive for the duration of the function.
    ComPtr<ITexture> tempTexture;

    TextureImpl* stagingTextureImpl = nullptr;
    uint32_t subResourceIdx = D3D11CalcSubresource(mip, layer, desc.mipCount);
    if (desc.memoryType == MemoryType::ReadBack)
    {
        // The texture is already a staging texture, so we can just use it directly.
        stagingTextureImpl = textureImpl;
    }
    else
    {
        // Due to complexity of texture creation, create a full slang-rhi texture set as read-back
        // rather than try to create a device version
        TextureDesc copyDesc = textureImpl->getDesc();
        copyDesc.memoryType = MemoryType::ReadBack;
        copyDesc.usage = TextureUsage::CopyDestination;

        // We just want to create a texture to copy the single subresource, so:
        // - Reduce dimensions to that of the mip level
        // - Only 1 mip level
        // - Arrays turn into their non-array counterpart
        // - Cube maps turn into 2D textures (as we only want 1 face)

        // Adjust mips, size and array
        copyDesc.mipCount = 1;
        copyDesc.size = layout.size;
        copyDesc.arrayLength = 1;

        // Ensure width/height of subresource are large enough to hold a block
        // for compressed textures.
        copyDesc.size.width = math::calcAligned2(copyDesc.size.width, layout.blockWidth);
        copyDesc.size.height = math::calcAligned2(copyDesc.size.height, layout.blockHeight);

        // Change type
        switch (copyDesc.type)
        {
        case TextureType::Texture1DArray:
            copyDesc.type = TextureType::Texture1D;
            break;
        case TextureType::Texture2DArray:
        case TextureType::TextureCube:
        case TextureType::TextureCubeArray:
            copyDesc.type = TextureType::Texture2D;
            break;
        case TextureType::Texture2DMSArray:
            copyDesc.type = TextureType::Texture2DMS;
            break;
        default:
            break;
        }

        // Create texture + do a few checks to make sure logic is correct
        SLANG_RETURN_ON_FAIL(createTexture(copyDesc, nullptr, tempTexture.writeRef()));
        stagingTextureImpl = checked_cast<TextureImpl*>(tempTexture.get());
        SLANG_RHI_ASSERT(stagingTextureImpl->getDesc().mipCount == 1);
        SLANG_RHI_ASSERT(stagingTextureImpl->getDesc().getLayerCount() == 1);

        // Copy the source subresource to subresource 0 of the staging texture, then switch
        // the subresource to be copied from to 0.
        m_immediateContext->CopySubresourceRegion(
            stagingTextureImpl->m_resource.get(),
            0,
            0,
            0,
            0,
            textureImpl->m_resource.get(),
            subResourceIdx,
            nullptr
        );
        subResourceIdx = 0;
    }

    // Now just read back texels from the staging textures
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        SLANG_RETURN_ON_FAIL(
            m_immediateContext
                ->Map(stagingTextureImpl->m_resource.get(), subResourceIdx, D3D11_MAP_READ, 0, &mappedResource)
        );

        auto blob = OwnedBlob::create(layout.sizeInBytes);
        uint8_t* srcBuffer = (uint8_t*)mappedResource.pData;
        uint8_t* dstBuffer = (uint8_t*)outData;

        // Data should be the same, but alignment may not be, so the row copy
        // needs to be the minimum of the two row sizes.
        uint32_t copyPitch = min(layout.rowPitch, (size_t)mappedResource.RowPitch);

        // Copy a row at a time.
        for (int z = 0; z < layout.size.depth; z++)
        {
            uint8_t* srcRow = srcBuffer;
            uint8_t* dstRow = dstBuffer;
            for (int y = 0; y < layout.rowCount; y++)
            {
                std::memcpy(dstRow, srcRow, copyPitch);
                srcRow += mappedResource.RowPitch;
                dstRow += layout.rowPitch;
            }
            srcBuffer += mappedResource.DepthPitch;
            dstBuffer += layout.slicePitch;
        }

        // Make sure to unmap
        m_immediateContext->Unmap(stagingTextureImpl->m_resource.get(), subResourceIdx);

        return SLANG_OK;
    }
}

Result DeviceImpl::readBuffer(IBuffer* buffer, Offset offset, Size size, void* outData)
{
    auto bufferImpl = checked_cast<BufferImpl*>(buffer);
    if (offset + size > bufferImpl->m_desc.size)
    {
        return SLANG_FAIL;
    }

    // Create staging buffer.
    ComPtr<ID3D11Buffer> stagingBuffer;
    D3D11_BUFFER_DESC stagingBufferDesc = {};
    stagingBufferDesc.ByteWidth = size;
    stagingBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingBufferDesc.Usage = D3D11_USAGE_STAGING;
    SLANG_RETURN_ON_FAIL(m_device->CreateBuffer(&stagingBufferDesc, nullptr, stagingBuffer.writeRef()));

    // Copy to staging buffer.
    D3D11_BOX srcBox = {};
    srcBox.left = (UINT)offset;
    srcBox.right = (UINT)(offset + size);
    srcBox.bottom = srcBox.back = 1;
    m_immediateContext->CopySubresourceRegion(stagingBuffer, 0, 0, 0, 0, bufferImpl->m_buffer, 0, &srcBox);

    // Map the staging buffer and copy data.
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    SLANG_RETURN_ON_FAIL(m_immediateContext->Map(stagingBuffer, 0, D3D11_MAP_READ, 0, &mappedResource));
    std::memcpy(outData, mappedResource.pData, size);
    m_immediateContext->Unmap(stagingBuffer, 0);

    return SLANG_OK;
}

Result DeviceImpl::getQueue(QueueType type, ICommandQueue** outQueue)
{
    if (type != QueueType::Graphics)
    {
        return SLANG_FAIL;
    }
    m_queue->establishStrongReferenceToDevice();
    returnComPtr(outQueue, m_queue);
    return SLANG_OK;
}

Result DeviceImpl::getTextureRowAlignment(Format format, Size* outAlignment)
{
    *outAlignment = 256;
    return SLANG_OK;
}

Result DeviceImpl::createShaderProgram(
    const ShaderProgramDesc& desc,
    IShaderProgram** outProgram,
    ISlangBlob** outDiagnosticBlob
)
{
    RefPtr<ShaderProgramImpl> shaderProgram = new ShaderProgramImpl(this, desc);
    SLANG_RETURN_ON_FAIL(shaderProgram->init());
    SLANG_RETURN_ON_FAIL(
        RootShaderObjectLayoutImpl::create(
            this,
            shaderProgram->linkedProgram,
            shaderProgram->linkedProgram->getLayout(),
            shaderProgram->m_rootObjectLayout.writeRef()
        )
    );
    returnComPtr(outProgram, shaderProgram);
    return SLANG_OK;
}

Result DeviceImpl::createShaderObjectLayout(
    slang::ISession* session,
    slang::TypeLayoutReflection* typeLayout,
    ShaderObjectLayout** outLayout
)
{
    RefPtr<ShaderObjectLayoutImpl> layout;
    SLANG_RETURN_ON_FAIL(ShaderObjectLayoutImpl::createForElementType(this, session, typeLayout, layout.writeRef()));
    returnRefPtrMove(outLayout, layout);
    return SLANG_OK;
}

Result DeviceImpl::createRootShaderObjectLayout(
    slang::IComponentType* program,
    slang::ProgramLayout* programLayout,
    ShaderObjectLayout** outLayout
)
{
    return SLANG_FAIL;
}

} // namespace rhi::d3d11

namespace rhi {

IAdapter* getD3D11Adapter(uint32_t index)
{
    std::vector<d3d11::AdapterImpl>& adapters = d3d11::getAdapters();
    return index < adapters.size() ? &adapters[index] : nullptr;
}

Result createD3D11Device(const DeviceDesc* desc, IDevice** outDevice)
{
    RefPtr<d3d11::DeviceImpl> result = new d3d11::DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(*desc));
    returnComPtr(outDevice, result);
    return SLANG_OK;
}

} // namespace rhi
