#define _CRT_SECURE_NO_WARNINGS
#include "d3d11-device.h"
#include "d3d11-buffer.h"
#include "d3d11-helper-functions.h"
#include "d3d11-query.h"
#include "d3d11-sampler.h"
#include "d3d11-scopeNVAPI.h"
#include "d3d11-shader-object-layout.h"
#include "d3d11-shader-object.h"
#include "d3d11-shader-program.h"
#include "d3d11-surface.h"
#include "d3d11-texture.h"
#include "d3d11-texture-view.h"
#include "d3d11-vertex-layout.h"

#include "core/string.h"

#if SLANG_RHI_ENABLE_AFTERMATH
#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_Defines.h"
#include "GFSDK_Aftermath_GpuCrashDump.h"
#endif

namespace rhi::d3d11 {

Result DeviceImpl::initialize(const DeviceDesc& desc)
{
    SLANG_RETURN_ON_FAIL(slangContext.initialize(
        desc.slang,
        desc.extendedDescCount,
        desc.extendedDescs,
        SLANG_DXBC,
        "sm_5_0",
        std::array{slang::PreprocessorMacroDesc{"__D3D11__", "1"}}
    ));

    SLANG_RETURN_ON_FAIL(Device::initialize(desc));

    // Initialize DeviceInfo
    {
        m_info.deviceType = DeviceType::D3D11;
        m_info.apiName = "D3D11";
        static const float kIdentity[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        ::memcpy(m_info.identityProjectionMatrix, kIdentity, sizeof(kIdentity));
    }

    m_desc = desc;

    // Rather than statically link against D3D, we load it dynamically.
    SharedLibraryHandle d3dModule;
#if SLANG_WINDOWS_FAMILY
    const char* const libName = "d3d11";
#else
    const char* const libName = "libdxvk_d3d11.so";
#endif
    if (SLANG_FAILED(loadSharedLibrary(libName, d3dModule)))
    {
        fprintf(stderr, "error: failed to load '%s'\n", libName);
        return SLANG_FAIL;
    }

    PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN D3D11CreateDeviceAndSwapChain_ =
        (PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)findSymbolAddressByName(d3dModule, "D3D11CreateDeviceAndSwapChain");
    if (!D3D11CreateDeviceAndSwapChain_)
    {
        fprintf(stderr, "error: failed load symbol 'D3D11CreateDeviceAndSwapChain'\n");
        return SLANG_FAIL;
    }

    PFN_D3D11_CREATE_DEVICE D3D11CreateDevice_ =
        (PFN_D3D11_CREATE_DEVICE)findSymbolAddressByName(d3dModule, "D3D11CreateDevice");
    if (!D3D11CreateDevice_)
    {
        fprintf(stderr, "error: failed load symbol 'D3D11CreateDevice'\n");
        return SLANG_FAIL;
    }

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
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_9_1;
    const int totalNumFeatureLevels = SLANG_COUNT_OF(featureLevels);

    {
        // On a machine that does not have an up-to-date version of D3D installed,
        // the `D3D11CreateDeviceAndSwapChain` call will fail with `E_INVALIDARG`
        // if you ask for feature level 11_1 (DeviceCheckFlag::UseFullFeatureLevel).
        // The workaround is to call `D3D11CreateDeviceAndSwapChain` the first time
        // with 11_1 and then back off to 11_0 if that fails.

        FlagCombiner combiner;
        // TODO: we should probably provide a command-line option
        // to override UseDebug of default rather than leave it
        // up to each back-end to specify.

#if _DEBUG
        /// First try debug then non debug
        combiner.add(DeviceCheckFlag::UseDebug, ChangeType::OnOff);
#else
        /// Don't bother with debug
        combiner.add(DeviceCheckFlag::UseDebug, ChangeType::Off);
#endif
        /// First try hardware, then reference
        combiner.add(DeviceCheckFlag::UseHardwareDevice, ChangeType::OnOff);
        /// First try fully featured, then degrade features
        combiner.add(DeviceCheckFlag::UseFullFeatureLevel, ChangeType::OnOff);

        const int numCombinations = combiner.getNumCombinations();
        Result res = SLANG_FAIL;
        for (int i = 0; i < numCombinations; ++i)
        {
            const auto deviceCheckFlags = combiner.getCombination(i);
            D3DUtil::createFactory(deviceCheckFlags, m_dxgiFactory);

            // If we have an adapter set on the desc, look it up.
            ComPtr<IDXGIAdapter> adapter;
            if (desc.adapterLUID)
            {
                std::vector<ComPtr<IDXGIAdapter>> dxgiAdapters;
                D3DUtil::findAdapters(deviceCheckFlags, desc.adapterLUID, m_dxgiFactory, dxgiAdapters);
                if (dxgiAdapters.empty())
                {
                    continue;
                }
                adapter = dxgiAdapters[0];
            }

            // The adapter can be nullptr - that just means 'default', but when so we need to select the driver type
            D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_UNKNOWN;
            if (adapter == nullptr)
            {
                // If we don't have an adapter, select directly
                driverType = (deviceCheckFlags & DeviceCheckFlag::UseHardwareDevice) ? D3D_DRIVER_TYPE_HARDWARE
                                                                                     : D3D_DRIVER_TYPE_REFERENCE;
            }

            const int startFeatureIndex = (deviceCheckFlags & DeviceCheckFlag::UseFullFeatureLevel) ? 0 : 1;
            const UINT deviceFlags = (deviceCheckFlags & DeviceCheckFlag::UseDebug) ? D3D11_CREATE_DEVICE_DEBUG : 0;

            res = D3D11CreateDevice_(
                adapter,
                driverType,
                nullptr,
                deviceFlags,
                &featureLevels[startFeatureIndex],
                totalNumFeatureLevels - startFeatureIndex,
                D3D11_SDK_VERSION,
                m_device.writeRef(),
                &featureLevel,
                m_immediateContext.writeRef()
            );

#if SLANG_RHI_ENABLE_AFTERMATH
            if (SLANG_SUCCEEDED(res))
            {
                if (deviceCheckFlags & DeviceCheckFlag::UseDebug)
                {
                    // Initialize Nsight Aftermath for this device.
                    // This combination of flags is not necessarily appropriate for real world usage
                    const uint32_t aftermathFlags =
                        GFSDK_Aftermath_FeatureFlags_EnableMarkers |      // Enable event marker tracking.
                        GFSDK_Aftermath_FeatureFlags_CallStackCapturing | // Enable automatic call stack event markers.
                        GFSDK_Aftermath_FeatureFlags_EnableResourceTracking |    // Enable tracking of resources.
                        GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo |   // Generate debug information for
                                                                                 // shaders.
                        GFSDK_Aftermath_FeatureFlags_EnableShaderErrorReporting; // Enable additional runtime shader
                                                                                 // error reporting.

                    auto initResult =
                        GFSDK_Aftermath_DX11_Initialize(GFSDK_Aftermath_Version_API, aftermathFlags, m_device);

                    if (initResult != GFSDK_Aftermath_Result_Success)
                    {
                        SLANG_RHI_ASSERT_FAILURE("Unable to initialize aftermath");
                        // Unable to initialize aftermath
                        return SLANG_FAIL;
                    }
                }
            }
#endif

            // Check if successfully constructed - if so we are done.
            if (SLANG_SUCCEEDED(res))
            {
                break;
            }
        }
        // If res is failure, means all styles have have failed, and so initialization fails.
        if (SLANG_FAILED(res))
        {
            return res;
        }
        // Check we have a swap chain, context and device
        SLANG_RHI_ASSERT(m_immediateContext && m_device);

        ComPtr<IDXGIDevice> dxgiDevice;
        if (m_device->QueryInterface(dxgiDevice.writeRef()) == 0)
        {
            ComPtr<IDXGIAdapter> dxgiAdapter;
            dxgiDevice->GetAdapter(dxgiAdapter.writeRef());
            DXGI_ADAPTER_DESC adapterDesc;
            dxgiAdapter->GetDesc(&adapterDesc);
            m_adapterName = string::from_wstring(adapterDesc.Description);
            m_info.adapterName = m_adapterName.data();
        }
    }

    // NVAPI
    if (desc.nvapiExtnSlot >= 0)
    {
        if (SLANG_FAILED(NVAPIUtil::initialize()))
        {
            return SLANG_E_NOT_AVAILABLE;
        }

#if SLANG_RHI_ENABLE_NVAPI
        if (NvAPI_D3D11_SetNvShaderExtnSlot(m_device, NvU32(desc.nvapiExtnSlot)) != NVAPI_OK)
        {
            return SLANG_E_NOT_AVAILABLE;
        }

        if (isSupportedNVAPIOp(m_device, NV_EXTN_OP_UINT64_ATOMIC))
        {
            m_features.add("atomic-int64");
        }
        if (isSupportedNVAPIOp(m_device, NV_EXTN_OP_FP32_ATOMIC))
        {
            m_features.add("atomic-float");
        }

        // If we have NVAPI well assume we have realtime clock
        {
            m_features.add("realtime-clock");
        }

        m_nvapi = true;
#endif
    }

    {
        // Create a TIMESTAMP_DISJOINT query object to query/update frequency info.
        D3D11_QUERY_DESC disjointQueryDesc = {};
        disjointQueryDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        SLANG_RETURN_ON_FAIL(m_device->CreateQuery(&disjointQueryDesc, m_disjointQuery.writeRef()));
        m_immediateContext->Begin(m_disjointQuery);
        m_immediateContext->End(m_disjointQuery);
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData = {};
        m_immediateContext->GetData(m_disjointQuery, &disjointData, sizeof(disjointData), 0);
        m_info.timestampFrequency = disjointData.Frequency;
    }

    // Get device limits.
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

        uint32_t maxColorAttachments = 4;
        if (featureLevel >= D3D_FEATURE_LEVEL_10_1)
            maxColorAttachments = 8;

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
        limits.maxTextureDimension1D = maxTextureDimensionUV;
        limits.maxTextureDimension2D = maxTextureDimensionUV;
        limits.maxTextureDimension3D = maxTextureDimensionW;
        limits.maxTextureDimensionCube = maxTextureDimensionCube;
        limits.maxTextureArrayLayers = maxTextureDimensionCube;

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

    return SLANG_OK;
}

Result DeviceImpl::createSurface(WindowHandle windowHandle, ISurface** outSurface)
{
    RefPtr<SurfaceImpl> surface = new SurfaceImpl();
    SLANG_RETURN_ON_FAIL(surface->init(this, windowHandle));
    returnComPtr(outSurface, surface);
    return SLANG_OK;
}

void DeviceImpl::beginRenderPass(const RenderPassDesc& desc)
{
    // Note: the framebuffer state will be flushed to the pipeline as part
    // of binding the root shader object.
    //
    // TODO: alternatively we could call `OMSetRenderTargets` here and then
    // call `OMSetRenderTargetsAndUnorderedAccessViews` later with the option
    // that preserves the existing RTV/DSV bindings.
    //
    m_d3dRenderTargetViews.resize(desc.colorAttachmentCount);
    for (Index i = 0; i < desc.colorAttachmentCount; ++i)
    {
        m_d3dRenderTargetViews[i] = static_cast<TextureViewImpl*>(desc.colorAttachments[i].view)->getRTV();
    }
    m_d3dDepthStencilView = desc.depthStencilAttachment
                                ? static_cast<TextureViewImpl*>(desc.depthStencilAttachment->view)->getDSV()
                                : nullptr;

    // Clear color attachments.
    for (Index i = 0; i < desc.colorAttachmentCount; ++i)
    {
        const auto& attachment = desc.colorAttachments[i];
        if (attachment.loadOp == LoadOp::Clear)
        {
            m_immediateContext->ClearRenderTargetView(
                static_cast<TextureViewImpl*>(attachment.view)->getRTV(),
                attachment.clearValue
            );
        }
    }
    // Clear depth/stencil attachment.
    if (desc.depthStencilAttachment)
    {
        const auto& attachment = *desc.depthStencilAttachment;
        UINT clearFlags = 0;
        if (attachment.depthLoadOp == LoadOp::Clear)
        {
            clearFlags |= D3D11_CLEAR_DEPTH;
        }
        if (attachment.stencilLoadOp == LoadOp::Clear)
        {
            clearFlags |= D3D11_CLEAR_STENCIL;
        }
        if (clearFlags)
        {
            m_immediateContext->ClearDepthStencilView(
                static_cast<TextureViewImpl*>(attachment.view)->getDSV(),
                D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                attachment.depthClearValue,
                attachment.stencilClearValue
            );
        }
    }
}

void DeviceImpl::endRenderPass()
{
    m_d3dRenderTargetViews.clear();
    m_d3dDepthStencilView = nullptr;
}

void DeviceImpl::setStencilReference(uint32_t referenceValue)
{
    m_stencilRef = referenceValue;
    m_depthStencilStateDirty = true;
}

Result DeviceImpl::readTexture(ITexture* texture, ISlangBlob** outBlob, size_t* outRowPitch, size_t* outPixelSize)
{
    auto textureImpl = static_cast<TextureImpl*>(texture);
    // Don't bother supporting MSAA for right now
    if (textureImpl->m_desc.sampleCount > 1)
    {
        fprintf(stderr, "ERROR: cannot capture multi-sample texture\n");
        return E_INVALIDARG;
    }

    const FormatInfo& formatInfo = getFormatInfo(textureImpl->m_desc.format);
    size_t bytesPerPixel = formatInfo.blockSizeInBytes / formatInfo.pixelsPerBlock;
    size_t rowPitch = int(textureImpl->m_desc.size.width) * bytesPerPixel;
    size_t bufferSize = rowPitch * int(textureImpl->m_desc.size.height);
    if (outRowPitch)
        *outRowPitch = rowPitch;
    if (outPixelSize)
        *outPixelSize = bytesPerPixel;

    D3D11_TEXTURE2D_DESC textureDesc;
    auto d3d11Texture = ((ID3D11Texture2D*)textureImpl->m_resource.get());
    d3d11Texture->GetDesc(&textureDesc);

    HRESULT hr = S_OK;
    ComPtr<ID3D11Texture2D> stagingTexture;

    if (textureDesc.Usage == D3D11_USAGE_STAGING && (textureDesc.CPUAccessFlags & D3D11_CPU_ACCESS_READ))
    {
        stagingTexture = d3d11Texture;
    }
    else
    {
        // Modify the descriptor to give us a staging texture
        textureDesc.BindFlags = 0;
        textureDesc.MiscFlags &= ~D3D11_RESOURCE_MISC_TEXTURECUBE;
        textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        textureDesc.Usage = D3D11_USAGE_STAGING;

        hr = m_device->CreateTexture2D(&textureDesc, 0, stagingTexture.writeRef());
        if (FAILED(hr))
        {
            fprintf(stderr, "ERROR: failed to create staging texture\n");
            return hr;
        }

        m_immediateContext->CopyResource(stagingTexture, d3d11Texture);
    }

    // Now just read back texels from the staging textures
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        SLANG_RETURN_ON_FAIL(m_immediateContext->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource));

        auto blob = OwnedBlob::create(bufferSize);
        char* buffer = (char*)blob->getBufferPointer();
        for (size_t y = 0; y < textureDesc.Height; y++)
        {
            memcpy(
                (char*)buffer + y * (*outRowPitch),
                (char*)mappedResource.pData + y * mappedResource.RowPitch,
                *outRowPitch
            );
        }
        // Make sure to unmap
        m_immediateContext->Unmap(stagingTexture, 0);

        returnComPtr(outBlob, blob);
        return SLANG_OK;
    }
}

Result DeviceImpl::createTexture(const TextureDesc& descIn, const SubresourceData* initData, ITexture** outTexture)
{
    TextureDesc srcDesc = fixupTextureDesc(descIn);

    const DXGI_FORMAT format = D3DUtil::getMapFormat(srcDesc.format);
    if (format == DXGI_FORMAT_UNKNOWN)
    {
        return SLANG_FAIL;
    }

    const int bindFlags = _calcResourceBindFlags(srcDesc.usage);

    // Set up the initialize data
    std::vector<D3D11_SUBRESOURCE_DATA> subRes;
    D3D11_SUBRESOURCE_DATA* subresourcesPtr = nullptr;
    if (initData)
    {
        int arrayLayerCount = srcDesc.arrayLength * (srcDesc.type == TextureType::TextureCube ? 6 : 1);
        subRes.resize(srcDesc.mipLevelCount * arrayLayerCount);
        {
            int subresourceIndex = 0;
            for (int i = 0; i < arrayLayerCount; i++)
            {
                for (int j = 0; j < srcDesc.mipLevelCount; j++)
                {
                    const int mipHeight = calcMipSize(srcDesc.size.height, j);

                    D3D11_SUBRESOURCE_DATA& data = subRes[subresourceIndex];
                    auto& srcData = initData[subresourceIndex];

                    data.pSysMem = srcData.data;
                    data.SysMemPitch = UINT(srcData.strideY);
                    data.SysMemSlicePitch = UINT(srcData.strideZ);

                    subresourceIndex++;
                }
            }
        }
        subresourcesPtr = subRes.data();
    }

    const int accessFlags = _calcResourceAccessFlags(srcDesc.memoryType);

    RefPtr<TextureImpl> texture(new TextureImpl(this, srcDesc));

    switch (srcDesc.type)
    {
    case TextureType::Texture1D:
    {
        D3D11_TEXTURE1D_DESC desc = {0};
        desc.BindFlags = bindFlags;
        desc.CPUAccessFlags = accessFlags;
        desc.Format = format;
        desc.MiscFlags = 0;
        desc.MipLevels = srcDesc.mipLevelCount;
        desc.ArraySize = srcDesc.arrayLength;
        desc.Width = srcDesc.size.width;
        desc.Usage = D3D11_USAGE_DEFAULT;

        ComPtr<ID3D11Texture1D> texture1D;
        SLANG_RETURN_ON_FAIL(m_device->CreateTexture1D(&desc, subresourcesPtr, texture1D.writeRef()));

        texture->m_resource = texture1D;
        break;
    }
    case TextureType::TextureCube:
    case TextureType::Texture2D:
    {
        D3D11_TEXTURE2D_DESC desc = {0};
        desc.BindFlags = bindFlags;
        desc.CPUAccessFlags = accessFlags;
        desc.Format = format;
        desc.MiscFlags = 0;
        desc.MipLevels = srcDesc.mipLevelCount;
        desc.ArraySize = srcDesc.arrayLength * (srcDesc.type == TextureType::TextureCube ? 6 : 1);

        desc.Width = srcDesc.size.width;
        desc.Height = srcDesc.size.height;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.SampleDesc.Count = srcDesc.sampleCount;
        desc.SampleDesc.Quality = srcDesc.sampleQuality;

        if (srcDesc.type == TextureType::TextureCube)
        {
            desc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
        }

        ComPtr<ID3D11Texture2D> texture2D;
        SLANG_RETURN_ON_FAIL(m_device->CreateTexture2D(&desc, subresourcesPtr, texture2D.writeRef()));

        texture->m_resource = texture2D;
        break;
    }
    case TextureType::Texture3D:
    {
        D3D11_TEXTURE3D_DESC desc = {0};
        desc.BindFlags = bindFlags;
        desc.CPUAccessFlags = accessFlags;
        desc.Format = format;
        desc.MiscFlags = 0;
        desc.MipLevels = srcDesc.mipLevelCount;
        desc.Width = srcDesc.size.width;
        desc.Height = srcDesc.size.height;
        desc.Depth = srcDesc.size.depth;
        desc.Usage = D3D11_USAGE_DEFAULT;

        ComPtr<ID3D11Texture3D> texture3D;
        SLANG_RETURN_ON_FAIL(m_device->CreateTexture3D(&desc, subresourcesPtr, texture3D.writeRef()));

        texture->m_resource = texture3D;
        break;
    }
    default:
        return SLANG_FAIL;
    }

    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

Result DeviceImpl::createBuffer(const BufferDesc& descIn, const void* initData, IBuffer** outBuffer)
{
    BufferDesc srcDesc = fixupBufferDesc(descIn);

    auto d3dBindFlags = _calcResourceBindFlags(srcDesc.usage);

    size_t alignedSizeInBytes = srcDesc.size;

    if (d3dBindFlags & D3D11_BIND_CONSTANT_BUFFER)
    {
        // Make aligned to 256 bytes... not sure why, but if you remove this the tests do fail.
        alignedSizeInBytes = D3DUtil::calcAligned(alignedSizeInBytes, 256);
    }

    // Hack to make the initialization never read from out of bounds memory, by copying into a buffer
    std::vector<uint8_t> initDataBuffer;
    if (initData && alignedSizeInBytes > srcDesc.size)
    {
        initDataBuffer.resize(alignedSizeInBytes);
        ::memcpy(initDataBuffer.data(), initData, srcDesc.size);
        initData = initDataBuffer.data();
    }

    D3D11_BUFFER_DESC bufferDesc = {0};
    bufferDesc.ByteWidth = UINT(alignedSizeInBytes);
    bufferDesc.BindFlags = d3dBindFlags;
    // For read we'll need to do some staging
    bufferDesc.CPUAccessFlags = _calcResourceAccessFlags(descIn.memoryType);
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;

    // If written by CPU, make it dynamic
    if (descIn.memoryType == MemoryType::Upload && !is_set(descIn.usage, BufferUsage::UnorderedAccess))
    {
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    }

    if (srcDesc.memoryType == MemoryType::ReadBack)
    {
        bufferDesc.CPUAccessFlags |= D3D11_CPU_ACCESS_READ;
        bufferDesc.Usage = D3D11_USAGE_STAGING;
    }

    switch (descIn.defaultState)
    {
    case ResourceState::ConstantBuffer:
    {
        // We'll just assume ConstantBuffers are dynamic for now
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        break;
    }
    default:
        break;
    }

    if (bufferDesc.BindFlags & (D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE))
    {
        // desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        if (srcDesc.elementSize != 0)
        {
            bufferDesc.StructureByteStride = (UINT)srcDesc.elementSize;
            bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        }
        else
        {
            bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        }
    }

    if (srcDesc.memoryType == MemoryType::Upload)
    {
        bufferDesc.CPUAccessFlags |= D3D11_CPU_ACCESS_WRITE;
    }

    D3D11_SUBRESOURCE_DATA subresourceData = {0};
    subresourceData.pSysMem = initData;

    RefPtr<BufferImpl> buffer(new BufferImpl(this, srcDesc));
    buffer->m_device = this;

    SLANG_RETURN_ON_FAIL(
        m_device->CreateBuffer(&bufferDesc, initData ? &subresourceData : nullptr, buffer->m_buffer.writeRef())
    );
    buffer->m_d3dUsage = bufferDesc.Usage;

    if (srcDesc.memoryType == MemoryType::ReadBack || bufferDesc.Usage != D3D11_USAGE_DYNAMIC)
    {
        D3D11_BUFFER_DESC bufDesc = {};
        bufDesc.BindFlags = 0;
        bufDesc.ByteWidth = (UINT)alignedSizeInBytes;
        bufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        bufDesc.Usage = D3D11_USAGE_STAGING;

        SLANG_RETURN_ON_FAIL(m_device->CreateBuffer(&bufDesc, nullptr, buffer->m_staging.writeRef()));
    }
    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::createSampler(SamplerDesc const& desc, ISampler** outSampler)
{
    D3D11_FILTER_REDUCTION_TYPE dxReduction = translateFilterReduction(desc.reductionOp);
    D3D11_FILTER dxFilter;
    if (desc.maxAnisotropy > 1)
    {
        dxFilter = D3D11_ENCODE_ANISOTROPIC_FILTER(dxReduction);
    }
    else
    {
        D3D11_FILTER_TYPE dxMin = translateFilterMode(desc.minFilter);
        D3D11_FILTER_TYPE dxMag = translateFilterMode(desc.magFilter);
        D3D11_FILTER_TYPE dxMip = translateFilterMode(desc.mipFilter);

        dxFilter = D3D11_ENCODE_BASIC_FILTER(dxMin, dxMag, dxMip, dxReduction);
    }

    D3D11_SAMPLER_DESC dxDesc = {};
    dxDesc.Filter = dxFilter;
    dxDesc.AddressU = translateAddressingMode(desc.addressU);
    dxDesc.AddressV = translateAddressingMode(desc.addressV);
    dxDesc.AddressW = translateAddressingMode(desc.addressW);
    dxDesc.MipLODBias = desc.mipLODBias;
    dxDesc.MaxAnisotropy = desc.maxAnisotropy;
    dxDesc.ComparisonFunc = translateComparisonFunc(desc.comparisonFunc);
    for (int ii = 0; ii < 4; ++ii)
        dxDesc.BorderColor[ii] = desc.borderColor[ii];
    dxDesc.MinLOD = desc.minLOD;
    dxDesc.MaxLOD = desc.maxLOD;

    ComPtr<ID3D11SamplerState> sampler;
    SLANG_RETURN_ON_FAIL(m_device->CreateSamplerState(&dxDesc, sampler.writeRef()));

    RefPtr<SamplerImpl> samplerImpl = new SamplerImpl(desc);
    samplerImpl->m_sampler = sampler;
    returnComPtr(outSampler, samplerImpl);
    return SLANG_OK;
}

Result DeviceImpl::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    RefPtr<TextureViewImpl> view = new TextureViewImpl(desc);
    view->m_texture = static_cast<TextureImpl*>(texture);
    if (view->m_desc.format == Format::Unknown)
        view->m_desc.format = view->m_texture->m_desc.format;
    view->m_desc.subresourceRange = view->m_texture->resolveSubresourceRange(desc.subresourceRange);
    returnComPtr(outView, view);
    return SLANG_OK;
}

Result DeviceImpl::createInputLayout(InputLayoutDesc const& desc, IInputLayout** outLayout)
{
    D3D11_INPUT_ELEMENT_DESC inputElements[16] = {};

    char hlslBuffer[1024];
    char* hlslCursor = &hlslBuffer[0];

    hlslCursor += sprintf(hlslCursor, "float4 main(\n");

    auto inputElementCount = desc.inputElementCount;
    auto inputElementsIn = desc.inputElements;
    for (Int ii = 0; ii < inputElementCount; ++ii)
    {
        auto vertexStreamIndex = inputElementsIn[ii].bufferSlotIndex;
        auto& vertexStream = desc.vertexStreams[vertexStreamIndex];

        inputElements[ii].SemanticName = inputElementsIn[ii].semanticName;
        inputElements[ii].SemanticIndex = (UINT)inputElementsIn[ii].semanticIndex;
        inputElements[ii].Format = D3DUtil::getMapFormat(inputElementsIn[ii].format);
        inputElements[ii].InputSlot = (UINT)vertexStreamIndex;
        inputElements[ii].AlignedByteOffset = (UINT)inputElementsIn[ii].offset;
        inputElements[ii].InputSlotClass = (vertexStream.slotClass == InputSlotClass::PerInstance)
                                               ? D3D11_INPUT_PER_INSTANCE_DATA
                                               : D3D11_INPUT_PER_VERTEX_DATA;
        inputElements[ii].InstanceDataStepRate = (UINT)vertexStream.instanceDataStepRate;

        if (ii != 0)
        {
            hlslCursor += sprintf(hlslCursor, ",\n");
        }

        char const* typeName = "Unknown";
        switch (inputElementsIn[ii].format)
        {
        case Format::R32G32B32A32_FLOAT:
        case Format::R8G8B8A8_UNORM:
            typeName = "float4";
            break;
        case Format::R32G32B32_FLOAT:
            typeName = "float3";
            break;
        case Format::R32G32_FLOAT:
            typeName = "float2";
            break;
        case Format::R32_FLOAT:
            typeName = "float";
            break;
        default:
            return SLANG_FAIL;
        }

        hlslCursor += sprintf(
            hlslCursor,
            "%s a%d : %s%d",
            typeName,
            (int)ii,
            inputElementsIn[ii].semanticName,
            (int)inputElementsIn[ii].semanticIndex
        );
    }

    hlslCursor += sprintf(hlslCursor, "\n) : SV_Position { return 0; }");

    ComPtr<ID3DBlob> vertexShaderBlob;
    SLANG_RETURN_ON_FAIL(D3DUtil::compileHLSLShader("inputLayout", hlslBuffer, "main", "vs_5_0", vertexShaderBlob));

    ComPtr<ID3D11InputLayout> inputLayout;
    SLANG_RETURN_ON_FAIL(m_device->CreateInputLayout(
        &inputElements[0],
        (UINT)inputElementCount,
        vertexShaderBlob->GetBufferPointer(),
        vertexShaderBlob->GetBufferSize(),
        inputLayout.writeRef()
    ));

    RefPtr<InputLayoutImpl> impl = new InputLayoutImpl;
    impl->m_layout.swap(inputLayout);

    auto vertexStreamCount = desc.vertexStreamCount;
    impl->m_vertexStreamStrides.resize(vertexStreamCount);
    for (Int i = 0; i < vertexStreamCount; ++i)
    {
        impl->m_vertexStreamStrides[i] = (UINT)desc.vertexStreams[i].stride;
    }

    returnComPtr(outLayout, impl);
    return SLANG_OK;
}

Result DeviceImpl::createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool)
{
    RefPtr<QueryPoolImpl> result = new QueryPoolImpl();
    SLANG_RETURN_ON_FAIL(result->init(desc, this));
    returnComPtr(outPool, result);
    return SLANG_OK;
}

void* DeviceImpl::map(IBuffer* bufferIn, MapFlavor flavor)
{
    BufferImpl* bufferImpl = static_cast<BufferImpl*>(bufferIn);

    D3D11_MAP mapType;
    ID3D11Buffer* buffer = bufferImpl->m_buffer;

    switch (flavor)
    {
    case MapFlavor::WriteDiscard:
        mapType = D3D11_MAP_WRITE_DISCARD;
        break;
    case MapFlavor::HostWrite:
        mapType = D3D11_MAP_WRITE_NO_OVERWRITE;
        break;
    case MapFlavor::HostRead:
        mapType = D3D11_MAP_READ;
        break;
    default:
        return nullptr;
    }

    bufferImpl->m_mapFlavor = flavor;

    switch (flavor)
    {
    case MapFlavor::WriteDiscard:
    case MapFlavor::HostWrite:
        // If buffer is not dynamic, we need to use staging buffer.
        if (bufferImpl->m_d3dUsage != D3D11_USAGE_DYNAMIC)
        {
            bufferImpl->m_uploadStagingBuffer.resize(bufferImpl->m_desc.size);
            return bufferImpl->m_uploadStagingBuffer.data();
        }
        break;
    case MapFlavor::HostRead:
        buffer = bufferImpl->m_staging;
        if (!buffer)
        {
            return nullptr;
        }

        // Okay copy the data over
        m_immediateContext->CopyResource(buffer, bufferImpl->m_buffer);
    }

    // We update our constant buffer per-frame, just for the purposes
    // of the example, but we don't actually load different data
    // per-frame (we always use an identity projection).
    D3D11_MAPPED_SUBRESOURCE mappedSub;
    SLANG_RETURN_NULL_ON_FAIL(m_immediateContext->Map(buffer, 0, mapType, 0, &mappedSub));

    return mappedSub.pData;
}

void DeviceImpl::unmap(IBuffer* bufferIn, size_t offsetWritten, size_t sizeWritten)
{
    BufferImpl* bufferImpl = static_cast<BufferImpl*>(bufferIn);
    switch (bufferImpl->m_mapFlavor)
    {
    case MapFlavor::WriteDiscard:
    case MapFlavor::HostWrite:
        // If buffer is not dynamic, the CPU has already written to the staging buffer,
        // and we need to copy the content over to the GPU buffer.
        if (bufferImpl->m_d3dUsage != D3D11_USAGE_DYNAMIC && sizeWritten != 0)
        {
            D3D11_BOX dstBox = {};
            dstBox.left = (UINT)offsetWritten;
            dstBox.right = (UINT)(offsetWritten + sizeWritten);
            dstBox.back = 1;
            dstBox.bottom = 1;
            m_immediateContext->UpdateSubresource(
                bufferImpl->m_buffer,
                0,
                &dstBox,
                bufferImpl->m_uploadStagingBuffer.data() + offsetWritten,
                0,
                0
            );
            return;
        }
    }
    m_immediateContext->Unmap(
        bufferImpl->m_mapFlavor == MapFlavor::HostRead ? bufferImpl->m_staging : bufferImpl->m_buffer,
        0
    );
}

#if 0
void D3D11Device::setInputLayout(InputLayout* inputLayoutIn)
{
    auto inputLayout = static_cast<InputLayoutImpl*>(inputLayoutIn);
    m_immediateContext->IASetInputLayout(inputLayout->m_layout);
}
#endif

void DeviceImpl::setVertexBuffers(
    GfxIndex startSlot,
    GfxCount slotCount,
    IBuffer* const* buffersIn,
    const Offset* offsetsIn
)
{
    static const int kMaxVertexBuffers = 16;
    SLANG_RHI_ASSERT(slotCount <= kMaxVertexBuffers);
    SLANG_RHI_ASSERT(m_currentPipeline); // The pipeline state should be created before setting vertex buffers.

    UINT vertexStrides[kMaxVertexBuffers];
    UINT vertexOffsets[kMaxVertexBuffers];
    ID3D11Buffer* dxBuffers[kMaxVertexBuffers];

    auto buffers = (BufferImpl* const*)buffersIn;

    for (GfxIndex ii = 0; ii < slotCount; ++ii)
    {
        auto inputLayout = (InputLayoutImpl*)m_currentPipeline->inputLayout.Ptr();
        vertexStrides[ii] = inputLayout->m_vertexStreamStrides[startSlot + ii];
        vertexOffsets[ii] = (UINT)offsetsIn[ii];
        dxBuffers[ii] = buffers[ii]->m_buffer;
    }

    m_immediateContext
        ->IASetVertexBuffers((UINT)startSlot, (UINT)slotCount, dxBuffers, &vertexStrides[0], &vertexOffsets[0]);
}

void DeviceImpl::setIndexBuffer(IBuffer* buffer, IndexFormat indexFormat, Offset offset)
{
    DXGI_FORMAT dxFormat = D3DUtil::getIndexFormat(indexFormat);
    m_immediateContext->IASetIndexBuffer(((BufferImpl*)buffer)->m_buffer, dxFormat, UINT(offset));
}

void DeviceImpl::setViewports(GfxCount count, Viewport const* viewports)
{
    static const int kMaxViewports = D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX + 1;
    SLANG_RHI_ASSERT(count <= kMaxViewports);

    D3D11_VIEWPORT dxViewports[kMaxViewports];
    for (GfxIndex ii = 0; ii < count; ++ii)
    {
        auto& inViewport = viewports[ii];
        auto& dxViewport = dxViewports[ii];

        dxViewport.TopLeftX = inViewport.originX;
        dxViewport.TopLeftY = inViewport.originY;
        dxViewport.Width = inViewport.extentX;
        dxViewport.Height = inViewport.extentY;
        dxViewport.MinDepth = inViewport.minZ;
        dxViewport.MaxDepth = inViewport.maxZ;
    }

    m_immediateContext->RSSetViewports(UINT(count), dxViewports);
}

void DeviceImpl::setScissorRects(GfxCount count, ScissorRect const* rects)
{
    static const int kMaxScissorRects = D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX + 1;
    SLANG_RHI_ASSERT(count <= kMaxScissorRects);

    D3D11_RECT dxRects[kMaxScissorRects];
    for (GfxIndex ii = 0; ii < count; ++ii)
    {
        auto& inRect = rects[ii];
        auto& dxRect = dxRects[ii];

        dxRect.left = LONG(inRect.minX);
        dxRect.top = LONG(inRect.minY);
        dxRect.right = LONG(inRect.maxX);
        dxRect.bottom = LONG(inRect.maxY);
    }

    m_immediateContext->RSSetScissorRects(UINT(count), dxRects);
}

void DeviceImpl::setPipeline(IPipeline* state)
{
    auto pipelineType = static_cast<Pipeline*>(state)->desc.type;

    switch (pipelineType)
    {
    default:
        break;

    case PipelineType::Graphics:
    {
        auto stateImpl = (GraphicsPipelineImpl*)state;
        auto programImpl = static_cast<ShaderProgramImpl*>(stateImpl->m_program.Ptr());

        // TODO: We could conceivably do some lightweight state
        // differencing here (e.g., check if `programImpl` is the
        // same as the program that is currently bound).
        //
        // It isn't clear how much that would pay off given that
        // the D3D11 runtime seems to do its own state diffing.

        // IA

        m_immediateContext->IASetInputLayout(stateImpl->m_inputLayout->m_layout);

        m_immediateContext->IASetPrimitiveTopology(
            D3DUtil::getPrimitiveTopology(stateImpl->desc.graphics.primitiveTopology)
        );

        // VS

        // TODO(tfoley): Why the conditional here? If somebody is trying to disable the VS or PS, shouldn't we respect
        // that?
        if (programImpl->m_vertexShader)
            m_immediateContext->VSSetShader(programImpl->m_vertexShader, nullptr, 0);

        // HS

        // DS

        // GS

        // RS

        m_immediateContext->RSSetState(stateImpl->m_rasterizerState);

        // PS
        if (programImpl->m_pixelShader)
            m_immediateContext->PSSetShader(programImpl->m_pixelShader, nullptr, 0);

        // OM

        m_immediateContext->OMSetBlendState(stateImpl->m_blendState, stateImpl->m_blendColor, stateImpl->m_sampleMask);

        m_currentPipeline = stateImpl;

        m_depthStencilStateDirty = true;
    }
    break;

    case PipelineType::Compute:
    {
        auto stateImpl = (ComputePipelineImpl*)state;
        auto programImpl = static_cast<ShaderProgramImpl*>(stateImpl->m_program.Ptr());

        // CS

        m_immediateContext->CSSetShader(programImpl->m_computeShader, nullptr, 0);
        m_currentPipeline = stateImpl;
    }
    break;
    }

    /// ...
}

void DeviceImpl::draw(GfxCount vertexCount, GfxIndex startVertex)
{
    _flushGraphicsState();
    m_immediateContext->Draw(vertexCount, startVertex);
}

void DeviceImpl::drawIndexed(GfxCount indexCount, GfxIndex startIndex, GfxIndex baseVertex)
{
    _flushGraphicsState();
    m_immediateContext->DrawIndexed(indexCount, startIndex, baseVertex);
}

void DeviceImpl::drawInstanced(
    GfxCount vertexCount,
    GfxCount instanceCount,
    GfxIndex startVertex,
    GfxIndex startInstanceLocation
)
{
    _flushGraphicsState();
    m_immediateContext->DrawInstanced(vertexCount, instanceCount, startVertex, startInstanceLocation);
}

void DeviceImpl::drawIndexedInstanced(
    GfxCount indexCount,
    GfxCount instanceCount,
    GfxIndex startIndexLocation,
    GfxIndex baseVertexLocation,
    GfxIndex startInstanceLocation
)
{
    _flushGraphicsState();
    m_immediateContext->DrawIndexedInstanced(
        indexCount,
        instanceCount,
        startIndexLocation,
        baseVertexLocation,
        startInstanceLocation
    );
}

Result DeviceImpl::createShaderProgram(
    const ShaderProgramDesc& desc,
    IShaderProgram** outProgram,
    ISlangBlob** outDiagnosticBlob
)
{
    SLANG_RHI_ASSERT(desc.slangGlobalScope);

    if (desc.slangGlobalScope->getSpecializationParamCount() != 0)
    {
        // For a specializable program, we don't invoke any actual slang compilation yet.
        RefPtr<ShaderProgramImpl> shaderProgram = new ShaderProgramImpl();
        shaderProgram->init(desc);
        returnComPtr(outProgram, shaderProgram);
        return SLANG_OK;
    }

    // If the program is already specialized, compile and create shader kernels now.
    SlangInt targetIndex = 0;
    auto slangGlobalScope = desc.slangGlobalScope;
    auto programLayout = slangGlobalScope->getLayout(targetIndex);
    if (!programLayout)
        return SLANG_FAIL;
    SlangUInt entryPointCount = programLayout->getEntryPointCount();
    if (entryPointCount == 0)
        return SLANG_FAIL;

    RefPtr<ShaderProgramImpl> shaderProgram = new ShaderProgramImpl();
    shaderProgram->slangGlobalScope = desc.slangGlobalScope;

    ScopeNVAPI scopeNVAPI;
    SLANG_RETURN_ON_FAIL(scopeNVAPI.init(this, 0));
    for (SlangUInt i = 0; i < entryPointCount; i++)
    {
        ComPtr<ISlangBlob> kernelCode;
        ComPtr<ISlangBlob> diagnostics;

        auto compileResult = getEntryPointCodeFromShaderCache(
            slangGlobalScope,
            (SlangInt)i,
            0,
            kernelCode.writeRef(),
            diagnostics.writeRef()
        );

        if (diagnostics)
        {
            DebugMessageType msgType = DebugMessageType::Warning;
            if (compileResult != SLANG_OK)
                msgType = DebugMessageType::Error;
            getDebugCallback()
                ->handleMessage(msgType, DebugMessageSource::Slang, (char*)diagnostics->getBufferPointer());
            if (outDiagnosticBlob)
                returnComPtr(outDiagnosticBlob, diagnostics);
        }

        SLANG_RETURN_ON_FAIL(compileResult);

        auto entryPoint = programLayout->getEntryPointByIndex(i);
        switch (entryPoint->getStage())
        {
        case SLANG_STAGE_COMPUTE:
            SLANG_RHI_ASSERT(entryPointCount == 1);
            SLANG_RETURN_ON_FAIL(m_device->CreateComputeShader(
                kernelCode->getBufferPointer(),
                kernelCode->getBufferSize(),
                nullptr,
                shaderProgram->m_computeShader.writeRef()
            ));
            break;
        case SLANG_STAGE_VERTEX:
            SLANG_RETURN_ON_FAIL(m_device->CreateVertexShader(
                kernelCode->getBufferPointer(),
                kernelCode->getBufferSize(),
                nullptr,
                shaderProgram->m_vertexShader.writeRef()
            ));
            break;
        case SLANG_STAGE_FRAGMENT:
            SLANG_RETURN_ON_FAIL(m_device->CreatePixelShader(
                kernelCode->getBufferPointer(),
                kernelCode->getBufferSize(),
                nullptr,
                shaderProgram->m_pixelShader.writeRef()
            ));
            break;
        default:
            SLANG_RHI_ASSERT_FAILURE("Pipeline stage not implemented");
        }
    }
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

Result DeviceImpl::createShaderObject(ShaderObjectLayout* layout, IShaderObject** outObject)
{
    RefPtr<ShaderObjectImpl> shaderObject;
    SLANG_RETURN_ON_FAIL(
        ShaderObjectImpl::create(this, static_cast<ShaderObjectLayoutImpl*>(layout), shaderObject.writeRef())
    );
    returnComPtr(outObject, shaderObject);
    return SLANG_OK;
}

Result DeviceImpl::createMutableShaderObject(ShaderObjectLayout* layout, IShaderObject** outObject)
{
    auto layoutImpl = static_cast<ShaderObjectLayoutImpl*>(layout);

    RefPtr<MutableShaderObjectImpl> result = new MutableShaderObjectImpl();
    SLANG_RETURN_ON_FAIL(result->init(this, layoutImpl));
    returnComPtr(outObject, result);

    return SLANG_OK;
}

Result DeviceImpl::createRootShaderObject(IShaderProgram* program, ShaderObjectBase** outObject)
{
    auto programImpl = static_cast<ShaderProgramImpl*>(program);
    RefPtr<RootShaderObjectImpl> shaderObject;
    RefPtr<RootShaderObjectLayoutImpl> rootLayout;
    SLANG_RETURN_ON_FAIL(RootShaderObjectLayoutImpl::create(
        this,
        programImpl->slangGlobalScope,
        programImpl->slangGlobalScope->getLayout(),
        rootLayout.writeRef()
    ));
    SLANG_RETURN_ON_FAIL(RootShaderObjectImpl::create(this, rootLayout.Ptr(), shaderObject.writeRef()));
    returnRefPtrMove(outObject, shaderObject);
    return SLANG_OK;
}

void DeviceImpl::bindRootShaderObject(IShaderObject* shaderObject)
{
    RootShaderObjectImpl* rootShaderObjectImpl = static_cast<RootShaderObjectImpl*>(shaderObject);
    RefPtr<Pipeline> specializedPipeline;
    maybeSpecializePipeline(m_currentPipeline, rootShaderObjectImpl, specializedPipeline);
    PipelineImpl* specializedPipelineImpl = static_cast<PipelineImpl*>(specializedPipeline.Ptr());
    setPipeline(specializedPipelineImpl);

    // In order to bind the root object we must compute its specialized layout.
    //
    // TODO: This is in most ways redundant with `maybeSpecializePipeline` above,
    // and the two operations should really be one.
    //
    RefPtr<ShaderObjectLayoutImpl> specializedRootLayout;
    rootShaderObjectImpl->_getSpecializedLayout(specializedRootLayout.writeRef());
    RootShaderObjectLayoutImpl* specializedRootLayoutImpl =
        static_cast<RootShaderObjectLayoutImpl*>(specializedRootLayout.Ptr());

    // Depending on whether we are binding a compute or a graphics/rasterization
    // pipeline, we will need to bind any SRVs/UAVs/CBs/samplers using different
    // D3D11 calls. We deal with that distinction here by instantiating an
    // appropriate subtype of `BindingContext` based on the pipeline type.
    //
    switch (m_currentPipeline->desc.type)
    {
    case PipelineType::Compute:
    {
        ComputeBindingContext context(this, m_immediateContext);
        rootShaderObjectImpl->bindAsRoot(&context, specializedRootLayoutImpl);

        // Because D3D11 requires all UAVs to be set at once, we did *not* issue
        // actual binding calls during the `bindAsRoot` step, and instead we
        // batch them up and set them here.
        //
        m_immediateContext->CSSetUnorderedAccessViews(0, context.uavCount, context.uavs, nullptr);
    }
    break;
    default:
    {
        GraphicsBindingContext context(this, m_immediateContext);
        rootShaderObjectImpl->bindAsRoot(&context, specializedRootLayoutImpl);

        // Similar to the compute case above, the rasteirzation case needs to
        // set the UAVs after the call to `bindAsRoot()` completes, but we
        // also have a few extra wrinkles here that are specific to the D3D 11.0
        // rasterization pipeline.
        //
        // In D3D 11.0, the RTV and UAV binding slots alias, so that a shader
        // that binds an RTV for `SV_Target0` cannot also bind a UAV for `u0`.
        // The Slang layout algorithm already accounts for this rule, and assigns
        // all UAVs to slots taht won't alias the RTVs it knows about.
        //
        // In order to account for the aliasing, we need to consider how many
        // RTVs are bound as part of the active framebuffer, and then adjust
        // the UAVs that we bind accordingly.
        //
        auto rtvCount = (UINT)m_d3dRenderTargetViews.size();
        //
        // The `context` we are using will have computed the number of UAV registers
        // that might need to be bound, as a range from 0 to `context.uavCount`.
        // However we need to skip over the first `rtvCount` of those, so the
        // actual number of UAVs we wnat to bind is smaller:
        //
        // Note: As a result we expect that either there were no UAVs bound,
        // *or* the number of UAV slots bound is higher than the number of
        // RTVs so that there is something left to actually bind.
        //
        SLANG_RHI_ASSERT((context.uavCount == 0) || (context.uavCount >= rtvCount));
        auto bindableUAVCount = context.uavCount - rtvCount;
        //
        // Similarly, the actual UAVs we intend to bind will come after the first
        // `rtvCount` in the array.
        //
        auto bindableUAVs = context.uavs + rtvCount;

        // Once the offsetting is accounted for, we set all of the RTVs, DSV,
        // and UAVs with one call.
        //
        // TODO: We may want to use the capability for `OMSetRenderTargetsAnd...`
        // to only set the UAVs and leave the RTVs/UAVs alone, so that we don't
        // needlessly re-bind RTVs during a pass.
        //
        m_immediateContext->OMSetRenderTargetsAndUnorderedAccessViews(
            rtvCount,
            m_d3dRenderTargetViews.data(),
            m_d3dDepthStencilView,
            rtvCount,
            bindableUAVCount,
            bindableUAVs,
            nullptr
        );
    }
    break;
    }
}

Result DeviceImpl::createRenderPipeline(const RenderPipelineDesc& inDesc, IPipeline** outPipeline)
{
    RenderPipelineDesc desc = inDesc;

    auto programImpl = (ShaderProgramImpl*)desc.program;

    ComPtr<ID3D11DepthStencilState> depthStencilState;
    {
        D3D11_DEPTH_STENCIL_DESC dsDesc;
        dsDesc.DepthEnable = desc.depthStencil.depthTestEnable;
        dsDesc.DepthWriteMask =
            desc.depthStencil.depthWriteEnable ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        dsDesc.DepthFunc = translateComparisonFunc(desc.depthStencil.depthFunc);
        dsDesc.StencilEnable = desc.depthStencil.stencilEnable;
        dsDesc.StencilReadMask = desc.depthStencil.stencilReadMask;
        dsDesc.StencilWriteMask = desc.depthStencil.stencilWriteMask;

#define FACE(DST, SRC)                                                                                                 \
    dsDesc.DST.StencilFailOp = translateStencilOp(desc.depthStencil.SRC.stencilFailOp);                                \
    dsDesc.DST.StencilDepthFailOp = translateStencilOp(desc.depthStencil.SRC.stencilDepthFailOp);                      \
    dsDesc.DST.StencilPassOp = translateStencilOp(desc.depthStencil.SRC.stencilPassOp);                                \
    dsDesc.DST.StencilFunc = translateComparisonFunc(desc.depthStencil.SRC.stencilFunc);                               \
    /* end */

        FACE(FrontFace, frontFace);
        FACE(BackFace, backFace);

        SLANG_RETURN_ON_FAIL(m_device->CreateDepthStencilState(&dsDesc, depthStencilState.writeRef()));
    }

    ComPtr<ID3D11RasterizerState> rasterizerState;
    {
        D3D11_RASTERIZER_DESC rsDesc;
        rsDesc.FillMode = translateFillMode(desc.rasterizer.fillMode);
        rsDesc.CullMode = translateCullMode(desc.rasterizer.cullMode);
        rsDesc.FrontCounterClockwise = desc.rasterizer.frontFace == FrontFaceMode::Clockwise;
        rsDesc.DepthBias = desc.rasterizer.depthBias;
        rsDesc.DepthBiasClamp = desc.rasterizer.depthBiasClamp;
        rsDesc.SlopeScaledDepthBias = desc.rasterizer.slopeScaledDepthBias;
        rsDesc.DepthClipEnable = desc.rasterizer.depthClipEnable;
        rsDesc.ScissorEnable = desc.rasterizer.scissorEnable;
        rsDesc.MultisampleEnable = desc.rasterizer.multisampleEnable;
        rsDesc.AntialiasedLineEnable = desc.rasterizer.antialiasedLineEnable;

        SLANG_RETURN_ON_FAIL(m_device->CreateRasterizerState(&rsDesc, rasterizerState.writeRef()));
    }

    ComPtr<ID3D11BlendState> blendState;
    {
        D3D11_BLEND_DESC dstDesc = {};

        ColorTargetState defaultTargetState;

        static const UInt kMaxTargets = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
        int targetCount = desc.targetCount;
        if (targetCount > kMaxTargets)
            return SLANG_FAIL;

        for (GfxIndex ii = 0; ii < kMaxTargets; ++ii)
        {
            const ColorTargetState* targetState = nullptr;
            if (ii < targetCount)
            {
                targetState = &desc.targets[ii];
            }
            else if (targetCount == 0)
            {
                targetState = &defaultTargetState;
            }
            else
            {
                targetState = &desc.targets[targetCount - 1];
            }

            auto& srcTarget = *targetState;
            auto& dstTarget = dstDesc.RenderTarget[ii];

            if (isBlendDisabled(srcTarget))
            {
                dstTarget.BlendEnable = false;
                dstTarget.BlendOp = D3D11_BLEND_OP_ADD;
                dstTarget.BlendOpAlpha = D3D11_BLEND_OP_ADD;
                dstTarget.SrcBlend = D3D11_BLEND_ONE;
                dstTarget.SrcBlendAlpha = D3D11_BLEND_ONE;
                dstTarget.DestBlend = D3D11_BLEND_ZERO;
                dstTarget.DestBlendAlpha = D3D11_BLEND_ZERO;
            }
            else
            {
                dstTarget.BlendEnable = true;
                dstTarget.BlendOp = translateBlendOp(srcTarget.color.op);
                dstTarget.BlendOpAlpha = translateBlendOp(srcTarget.alpha.op);
                dstTarget.SrcBlend = translateBlendFactor(srcTarget.color.srcFactor);
                dstTarget.SrcBlendAlpha = translateBlendFactor(srcTarget.alpha.srcFactor);
                dstTarget.DestBlend = translateBlendFactor(srcTarget.color.dstFactor);
                dstTarget.DestBlendAlpha = translateBlendFactor(srcTarget.alpha.dstFactor);
            }

            dstTarget.RenderTargetWriteMask = translateRenderTargetWriteMask(srcTarget.writeMask);
        }

        dstDesc.IndependentBlendEnable = targetCount > 1;
        dstDesc.AlphaToCoverageEnable = desc.multisample.alphaToCoverageEnable;

        SLANG_RETURN_ON_FAIL(m_device->CreateBlendState(&dstDesc, blendState.writeRef()));
    }

    RefPtr<GraphicsPipelineImpl> pipeline = new GraphicsPipelineImpl();
    pipeline->m_depthStencilState = depthStencilState;
    pipeline->m_rasterizerState = rasterizerState;
    pipeline->m_blendState = blendState;
    pipeline->m_inputLayout = static_cast<InputLayoutImpl*>(desc.inputLayout);
    pipeline->m_rtvCount = desc.targetCount;
    pipeline->m_blendColor[0] = 0;
    pipeline->m_blendColor[1] = 0;
    pipeline->m_blendColor[2] = 0;
    pipeline->m_blendColor[3] = 0;
    pipeline->m_sampleMask = 0xFFFFFFFF;
    pipeline->init(desc);
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

Result DeviceImpl::createComputePipeline(const ComputePipelineDesc& inDesc, IPipeline** outPipeline)
{
    ComputePipelineDesc desc = inDesc;

    RefPtr<ComputePipelineImpl> state = new ComputePipelineImpl();
    state->init(desc);
    returnComPtr(outPipeline, state);
    return SLANG_OK;
}

void DeviceImpl::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    auto dstImpl = static_cast<BufferImpl*>(dst);
    auto srcImpl = static_cast<BufferImpl*>(src);
    D3D11_BOX srcBox = {};
    srcBox.left = (UINT)srcOffset;
    srcBox.right = (UINT)(srcOffset + size);
    srcBox.bottom = srcBox.back = 1;
    m_immediateContext
        ->CopySubresourceRegion(dstImpl->m_buffer, 0, (UINT)dstOffset, 0, 0, srcImpl->m_buffer, 0, &srcBox);
}

void DeviceImpl::dispatchCompute(int x, int y, int z)
{
    m_immediateContext->Dispatch(x, y, z);
}

void DeviceImpl::_flushGraphicsState()
{
    if (m_depthStencilStateDirty)
    {
        m_depthStencilStateDirty = false;
        auto pipeline = static_cast<GraphicsPipelineImpl*>(m_currentPipeline.Ptr());
        m_immediateContext->OMSetDepthStencilState(pipeline->m_depthStencilState, m_stencilRef);
    }
}

void DeviceImpl::beginCommandBuffer(const CommandBufferInfo& info)
{
    if (info.hasWriteTimestamps)
    {
        m_immediateContext->Begin(m_disjointQuery);
    }
}

void DeviceImpl::endCommandBuffer(const CommandBufferInfo& info)
{
    if (info.hasWriteTimestamps)
    {
        m_immediateContext->End(m_disjointQuery);
    }
}

void DeviceImpl::writeTimestamp(IQueryPool* pool, GfxIndex index)
{
    auto poolImpl = static_cast<QueryPoolImpl*>(pool);
    m_immediateContext->End(poolImpl->getQuery(index));
}

} // namespace rhi::d3d11
