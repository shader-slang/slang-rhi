#pragma once

#define SLANG_RHI_FORWARD_PASS_ENCODER_IMPL(PassEncoderBase)                                                           \
    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override         \
    {                                                                                                                  \
        return PassEncoderBase::queryInterface(uuid, outObject);                                                       \
    }                                                                                                                  \
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override                                                      \
    {                                                                                                                  \
        return PassEncoderBase::addRef();                                                                              \
    }                                                                                                                  \
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override                                                     \
    {                                                                                                                  \
        return PassEncoderBase::release();                                                                             \
    }                                                                                                                  \
    virtual SLANG_NO_THROW void SLANG_MCALL setBufferState(IBuffer* buffer, ResourceState state) override              \
    {                                                                                                                  \
        PassEncoderBase::setBufferState(buffer, state);                                                                \
    }                                                                                                                  \
    virtual SLANG_NO_THROW void SLANG_MCALL                                                                            \
    setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state) override                \
    {                                                                                                                  \
        PassEncoderBase::setTextureState(texture, subresourceRange, state);                                            \
    }                                                                                                                  \
    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* pool, GfxIndex index) override                  \
    {                                                                                                                  \
        PassEncoderBase::writeTimestamp(pool, index);                                                                  \
    }                                                                                                                  \
    virtual SLANG_NO_THROW void SLANG_MCALL beginDebugEvent(const char* name, float rgbColor[3]) override              \
    {                                                                                                                  \
        PassEncoderBase::beginDebugEvent(name, rgbColor);                                                              \
    }                                                                                                                  \
    virtual SLANG_NO_THROW void SLANG_MCALL endDebugEvent() override                                                   \
    {                                                                                                                  \
        PassEncoderBase::endDebugEvent();                                                                              \
    }
