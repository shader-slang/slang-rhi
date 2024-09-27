#pragma once

#include "rhi-shared.h"
// #include <slang-rhi.h>

#include <vector>
#include <map>

namespace rhi {

struct BufferState
{
    ResourceState state = ResourceState::Undefined;
};

struct TextureState
{
    ResourceState state = ResourceState::Undefined;
    std::vector<ResourceState> subresourceStates;
};

struct BufferBarrier
{
    Buffer* buffer;
    ResourceState stateBefore;
    ResourceState stateAfter;
};

struct TextureBarrier
{
    Texture* texture;
    ResourceState stateBefore;
    ResourceState stateAfter;
};

class StateTracking
{
public:
    void setBufferState(Buffer* buffer, ResourceState state)
    {
        if (buffer->m_desc.memoryType != MemoryType::DeviceLocal)
        {
            return;
        }

        BufferState* bufferState = getBufferState(buffer);
        if (state != bufferState->state || state == ResourceState::UnorderedAccess)
        {
            m_bufferBarriers.push_back({buffer, bufferState->state, state});
            bufferState->state = state;
        }
    }

    void setTextureState(Texture* texture, ResourceState state)
    {
        if (texture->m_desc.memoryType != MemoryType::DeviceLocal)
        {
            return;
        }

        TextureState* textureState = getTextureState(texture);
        if (state != textureState->state || state == ResourceState::UnorderedAccess)
        {
            m_textureBarriers.push_back({texture, textureState->state, state});
            textureState->state = state;
        }
    }

    void setTextureSubresourceState(Texture* texture, SubresourceRange subresourceRange, ResourceState state)
    {
        SLANG_RHI_ASSERT_FAILURE("Subresource state tracking not implemented");
    }

    void requireDefaultStates()
    {
        for (auto& bufferState : m_bufferStates)
        {
            if (bufferState.second.state != bufferState.first->m_desc.defaultState)
            {
                setBufferState(bufferState.first, bufferState.first->m_desc.defaultState);
            }
        }
        for (auto& textureState : m_textureStates)
        {
            if (textureState.second.state != textureState.first->m_desc.defaultState)
            {
                setTextureState(textureState.first, textureState.first->m_desc.defaultState);
            }
        }
    }

    const std::vector<BufferBarrier>& getBufferBarriers() const { return m_bufferBarriers; }

    const std::vector<TextureBarrier>& getTextureBarriers() const { return m_textureBarriers; }

    void clearBarriers()
    {
        m_bufferBarriers.clear();
        m_textureBarriers.clear();
    }

    void clear()
    {
        m_bufferStates.clear();
        m_textureStates.clear();
        clearBarriers();
    }

private:
    std::map<Buffer*, BufferState> m_bufferStates;
    std::map<Texture*, TextureState> m_textureStates;
    std::vector<BufferBarrier> m_bufferBarriers;
    std::vector<TextureBarrier> m_textureBarriers;

    BufferState* getBufferState(Buffer* buffer)
    {
        auto it = m_bufferStates.find(buffer);
        if (it != m_bufferStates.end())
            return &it->second;
        m_bufferStates[buffer] = {buffer->m_desc.defaultState};
        return &m_bufferStates[buffer];
    }

    TextureState* getTextureState(Texture* texture)
    {
        auto it = m_textureStates.find(texture);
        if (it != m_textureStates.end())
            return &it->second;
        m_textureStates[texture] = {texture->m_desc.defaultState};
        return &m_textureStates[texture];
    }
};

} // namespace rhi
