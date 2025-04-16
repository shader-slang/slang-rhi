#pragma once

#include "rhi-shared.h"

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
    bool entireTexture;
    uint32_t mip;
    uint32_t layer;
    ResourceState stateBefore;
    ResourceState stateAfter;
};

class StateTracking
{
public:
    void setBufferState(Buffer* buffer, ResourceState state)
    {
        // Cannot change state of upload/readback buffers.
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

    void setTextureState(Texture* texture, SubresourceRange subresourceRange, ResourceState state)
    {
        // Cannot change state of upload/readback buffers.
        if (texture->m_desc.memoryType != MemoryType::DeviceLocal)
        {
            return;
        }

        subresourceRange = texture->resolveSubresourceRange(subresourceRange);
        bool isEntireTexture = texture->isEntireTexture(subresourceRange);
        TextureState* textureState = getTextureState(texture);

        if (isEntireTexture && textureState->subresourceStates.empty())
        {
            // Transition entire texture.
            if (state != textureState->state || state == ResourceState::UnorderedAccess)
            {
                m_textureBarriers.push_back({texture, true, 0, 0, textureState->state, state});
                textureState->state = state;
            }
        }
        else
        {
            // Transition subresources.
            if (textureState->subresourceStates.empty())
            {
                textureState->subresourceStates.resize(texture->m_desc.getSubresourceCount(), textureState->state);
                textureState->state = ResourceState::Undefined;
            }
            uint32_t layerCount = texture->m_desc.getLayerCount();
            for (uint32_t layer = subresourceRange.layer; layer < layerCount; layer++)
            {
                for (uint32_t mip = subresourceRange.mip; mip < subresourceRange.mip + subresourceRange.mipCount; mip++)
                {
                    uint32_t subresourceIndex = layer * texture->m_desc.mipCount + mip;
                    if (state != textureState->subresourceStates[subresourceIndex] ||
                        state == ResourceState::UnorderedAccess)
                    {
                        m_textureBarriers.push_back({
                            texture,
                            false,
                            mip,
                            layer,
                            textureState->subresourceStates[subresourceIndex],
                            state,
                        });
                        textureState->subresourceStates[subresourceIndex] = state;
                    }
                }
            }

            // Check if all subresource states are equal and we can represent them as a single texture state.
            ResourceState commonState = textureState->subresourceStates[0];
            bool allEqual = true;
            for (ResourceState subresourceState : textureState->subresourceStates)
            {
                if (subresourceState != commonState)
                {
                    allEqual = false;
                    break;
                }
            }
            if (allEqual)
            {
                textureState->state = commonState;
                textureState->subresourceStates.clear();
            }
        }
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
                setTextureState(textureState.first, kEntireTexture, textureState.first->m_desc.defaultState);
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
