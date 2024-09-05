#pragma once

#define SLANG_RHI_FORWARD_COMMAND_ENCODER_IMPL(CommandEncoderBase)                                                     \
    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override         \
    {                                                                                                                  \
        return CommandEncoderBase::queryInterface(uuid, outObject);                                                    \
    }                                                                                                                  \
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override                                                      \
    {                                                                                                                  \
        return CommandEncoderBase::addRef();                                                                           \
    }                                                                                                                  \
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override                                                     \
    {                                                                                                                  \
        return CommandEncoderBase::release();                                                                          \
    }                                                                                                                  \
    virtual SLANG_NO_THROW void SLANG_MCALL                                                                            \
    textureBarrier(GfxCount count, ITexture* const* textures, ResourceState src, ResourceState dst) override           \
    {                                                                                                                  \
        CommandEncoderBase::textureBarrier(count, textures, src, dst);                                                 \
    }                                                                                                                  \
    virtual SLANG_NO_THROW void SLANG_MCALL textureSubresourceBarrier(                                                 \
        ITexture* texture,                                                                                             \
        SubresourceRange subresourceRange,                                                                             \
        ResourceState src,                                                                                             \
        ResourceState dst                                                                                              \
    ) override                                                                                                         \
    {                                                                                                                  \
        CommandEncoderBase::textureSubresourceBarrier(texture, subresourceRange, src, dst);                            \
    }                                                                                                                  \
    virtual SLANG_NO_THROW void SLANG_MCALL                                                                            \
    bufferBarrier(GfxCount count, IBuffer* const* buffers, ResourceState src, ResourceState dst) override              \
    {                                                                                                                  \
        CommandEncoderBase::bufferBarrier(count, buffers, src, dst);                                                   \
    }                                                                                                                  \
    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* pool, GfxIndex index) override                  \
    {                                                                                                                  \
        CommandEncoderBase::writeTimestamp(pool, index);                                                               \
    }                                                                                                                  \
    virtual SLANG_NO_THROW void SLANG_MCALL beginDebugEvent(const char* name, float rgbColor[3]) override              \
    {                                                                                                                  \
        CommandEncoderBase::beginDebugEvent(name, rgbColor);                                                           \
    }                                                                                                                  \
    virtual SLANG_NO_THROW void SLANG_MCALL endDebugEvent() override                                                   \
    {                                                                                                                  \
        CommandEncoderBase::endDebugEvent();                                                                           \
    }
