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
    virtual SLANG_NO_THROW void SLANG_MCALL setBufferState(IBuffer* buffer, ResourceState state) override              \
    {                                                                                                                  \
        CommandEncoderBase::setBufferState(buffer, state);                                                             \
    }                                                                                                                  \
    virtual SLANG_NO_THROW void SLANG_MCALL                                                                            \
    setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state) override                \
    {                                                                                                                  \
        CommandEncoderBase::setTextureState(texture, subresourceRange, state);                                         \
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
