#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

class ResourceCommandEncoderImpl : public IResourceCommandEncoder
{
public:
    CommandWriter* m_writer;

    virtual void* getInterface(SlangUUID const& uuid)
    {
        if (uuid == GUID::IID_IResourceCommandEncoder || uuid == ISlangUnknown::getTypeGuid())
            return this;
        return nullptr;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override
    {
        if (auto ptr = getInterface(uuid))
        {
            *outObject = ptr;
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
    }

    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }

    void init(CommandBufferImpl* cmdBuffer);

    virtual SLANG_NO_THROW void SLANG_MCALL endEncoding() override {}
    virtual SLANG_NO_THROW void SLANG_MCALL
    copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    textureBarrier(GfxCount count, ITexture* const* textures, ResourceState src, ResourceState dst) override
    {
    }

    virtual SLANG_NO_THROW void SLANG_MCALL
    bufferBarrier(GfxCount count, IBuffer* const* buffers, ResourceState src, ResourceState dst) override
    {
    }

    virtual SLANG_NO_THROW void SLANG_MCALL
    uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data) override;

    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* pool, GfxIndex index) override;

    virtual SLANG_NO_THROW void SLANG_MCALL copyTexture(
        ITexture* dst,
        ResourceState dstState,
        SubresourceRange dstSubresource,
        Offset3D dstOffset,
        ITexture* src,
        ResourceState srcState,
        SubresourceRange srcSubresource,
        Offset3D srcOffset,
        Extents extent
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL uploadTextureData(
        ITexture* dst,
        SubresourceRange subResourceRange,
        Offset3D offset,
        Extents extent,
        SubresourceData* subResourceData,
        GfxCount subResourceDataCount
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    clearResourceView(IResourceView* view, ClearValue* clearValue, ClearResourceViewFlags::Enum flags) override;

    virtual SLANG_NO_THROW void SLANG_MCALL resolveResource(
        ITexture* source,
        ResourceState sourceState,
        SubresourceRange sourceRange,
        ITexture* dest,
        ResourceState destState,
        SubresourceRange destRange
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    resolveQuery(IQueryPool* queryPool, GfxIndex index, GfxCount count, IBuffer* buffer, Offset offset) override;

    virtual SLANG_NO_THROW void SLANG_MCALL copyTextureToBuffer(
        IBuffer* dst,
        Offset dstOffset,
        Size dstSize,
        Size dstRowStride,
        ITexture* src,
        ResourceState srcState,
        SubresourceRange srcSubresource,
        Offset3D srcOffset,
        Extents extent
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL textureSubresourceBarrier(
        ITexture* texture,
        SubresourceRange subresourceRange,
        ResourceState src,
        ResourceState dst
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL beginDebugEvent(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL endDebugEvent() override {}
};

class ComputeCommandEncoderImpl : public IComputeCommandEncoder, public ResourceCommandEncoderImpl
{
public:
    SLANG_RHI_FORWARD_RESOURCE_COMMAND_ENCODER_IMPL(ResourceCommandEncoderImpl)

    virtual void* getInterface(SlangUUID const& uuid) override
    {
        if (uuid == GUID::IID_IResourceCommandEncoder || uuid == GUID::IID_IComputeCommandEncoder ||
            uuid == ISlangUnknown::getTypeGuid())
            return this;
        return nullptr;
    }

public:
    CommandWriter* m_writer;
    CommandBufferImpl* m_commandBuffer;
    RefPtr<ShaderObjectBase> m_rootObject;

    virtual SLANG_NO_THROW void SLANG_MCALL endEncoding() override {}

    void init(CommandBufferImpl* cmdBuffer);

    virtual SLANG_NO_THROW Result SLANG_MCALL bindPipeline(IPipeline* state, IShaderObject** outRootObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL dispatchCompute(int x, int y, int z) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL dispatchComputeIndirect(IBuffer* argBuffer, Offset offset) override;
};

} // namespace rhi::cuda
