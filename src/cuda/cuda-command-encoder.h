#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

class PassEncoderImpl : public IPassEncoder
{
public:
    CommandWriter* m_writer;

    virtual void* getInterface(SlangUUID const& uuid)
    {
        if (uuid == GUID::IID_IPassEncoder || uuid == ISlangUnknown::getTypeGuid())
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

public:
    void init(CommandBufferImpl* cmdBuffer);

    virtual SLANG_NO_THROW void SLANG_MCALL setBufferState(IBuffer* buffer, ResourceState state) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state) override;

    virtual SLANG_NO_THROW void SLANG_MCALL beginDebugEvent(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL endDebugEvent() override;

    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* pool, GfxIndex index) override;
};

class ResourcePassEncoderImpl : public IResourcePassEncoder, public PassEncoderImpl
{
public:
    SLANG_RHI_FORWARD_PASS_ENCODER_IMPL(PassEncoderImpl)

    virtual void* getInterface(SlangUUID const& uuid) override
    {
        if (uuid == GUID::IID_IResourcePassEncoder || uuid == GUID::IID_IPassEncoder ||
            uuid == ISlangUnknown::getTypeGuid())
            return this;
        return nullptr;
    }

public:
    virtual SLANG_NO_THROW void SLANG_MCALL end() override {}

    virtual SLANG_NO_THROW void SLANG_MCALL
    uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size) override;

    virtual SLANG_NO_THROW void SLANG_MCALL copyTexture(
        ITexture* dst,
        SubresourceRange dstSubresource,
        Offset3D dstOffset,
        ITexture* src,
        SubresourceRange srcSubresource,
        Offset3D srcOffset,
        Extents extent
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL uploadTextureData(
        ITexture* dst,
        SubresourceRange subresourceRange,
        Offset3D offset,
        Extents extent,
        SubresourceData* subresourceData,
        GfxCount subresourceDataCount
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL clearBuffer(IBuffer* buffer, const BufferRange* range) override;

    virtual SLANG_NO_THROW void SLANG_MCALL clearTexture(
        ITexture* texture,
        const ClearValue& clearValue,
        const SubresourceRange* subresourceRange,
        bool clearDepth,
        bool clearStencil
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    resolveQuery(IQueryPool* queryPool, GfxIndex index, GfxCount count, IBuffer* buffer, Offset offset) override;

    virtual SLANG_NO_THROW void SLANG_MCALL copyTextureToBuffer(
        IBuffer* dst,
        Offset dstOffset,
        Size dstSize,
        Size dstRowStride,
        ITexture* src,
        SubresourceRange srcSubresource,
        Offset3D srcOffset,
        Extents extent
    ) override;
};

class ComputePassEncoderImpl : public IComputePassEncoder, public PassEncoderImpl
{
public:
    SLANG_RHI_FORWARD_PASS_ENCODER_IMPL(PassEncoderImpl)

    virtual void* getInterface(SlangUUID const& uuid) override
    {
        if (uuid == GUID::IID_IComputePassEncoder || uuid == GUID::IID_IPassEncoder ||
            uuid == ISlangUnknown::getTypeGuid())
            return this;
        return nullptr;
    }

public:
    CommandWriter* m_writer;
    CommandBufferImpl* m_commandBuffer;
    RefPtr<ShaderObjectBase> m_rootObject;

    void init(CommandBufferImpl* cmdBuffer);

    virtual SLANG_NO_THROW void SLANG_MCALL end() override {}

    virtual SLANG_NO_THROW Result SLANG_MCALL bindPipeline(IPipeline* state, IShaderObject** outRootObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL dispatchCompute(int x, int y, int z) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL dispatchComputeIndirect(IBuffer* argBuffer, Offset offset) override;
};

} // namespace rhi::cuda
