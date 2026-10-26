#pragma once

#include "rhi-shared.h"

#include <algorithm>
#include <functional>
#include <map>
#include <span>
#include <vector>

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

    void resetBufferState(Buffer* buffer)
    {
        BufferState* bufferState = getBufferState(buffer);
        bufferState->state = ResourceState::Undefined;
    }

    void resetTextureState(Texture* texture)
    {
        TextureState* textureState = getTextureState(texture);
        textureState->state = ResourceState::Undefined;
        textureState->subresourceStates.clear();
    }

    // Activation invalidates every overlapping alias, including predecessors not explicitly
    // named by the caller. D3D12 must restore their transition states before activation can
    // overwrite compression metadata. Vulkan can simply forget their old layouts.
    void deactivateAliases(std::span<IResource* const> after, bool restoreDefaultStates)
    {
        if (after.empty() || (m_bufferStates.empty() && m_textureStates.empty()))
            return;

        struct PlacementRange
        {
            Resource* resource;
            ResourceHeap* heap;
            Offset begin;
            Offset end;
        };
        short_vector<PlacementRange, 16> destinations;
        destinations.reserve(after.size());
        for (IResource* entry : after)
        {
            Resource* resource = asBuffer(entry);
            if (!resource)
                resource = asTexture(entry);
            SLANG_RHI_ASSERT(resource && resource->isPlaced());
            const Offset begin = resource->m_placementOffset;
            destinations.push_back(
                {resource, resource->m_placementHeap.get(), begin, begin + resource->m_placementRequirements.size}
            );
        }
        std::sort(
            destinations.begin(),
            destinations.end(),
            [](const PlacementRange& a, const PlacementRange& b)
            {
                return a.heap != b.heap ? std::less<ResourceHeap*>()(a.heap, b.heap) : a.end < b.end;
            }
        );

        // Scan each state map once. Destinations are disjoint, so the first range in the
        // same heap ending after a resource begins is sufficient to determine overlap.
        auto overlaps = [&destinations](Resource* resource)
        {
            if (!resource->isPlaced())
                return false;
            auto it = std::upper_bound(
                destinations.begin(),
                destinations.end(),
                resource,
                [](Resource* candidate, const PlacementRange& range)
                {
                    ResourceHeap* heap = candidate->m_placementHeap.get();
                    return heap != range.heap ? std::less<ResourceHeap*>()(heap, range.heap)
                                              : candidate->m_placementOffset < range.end;
                }
            );
            return it != destinations.end() && it->heap == resource->m_placementHeap.get() &&
                   it->resource != resource &&
                   it->begin < resource->m_placementOffset + resource->m_placementRequirements.size;
        };
        for (auto it = m_bufferStates.begin(); it != m_bufferStates.end();)
        {
            Buffer* buffer = it->first;
            if (!overlaps(buffer))
            {
                ++it;
                continue;
            }
            if (restoreDefaultStates && it->second.state != buffer->m_desc.defaultState)
                setBufferState(buffer, buffer->m_desc.defaultState);
            it = m_bufferStates.erase(it);
        }
        for (auto it = m_textureStates.begin(); it != m_textureStates.end();)
        {
            Texture* texture = it->first;
            if (!overlaps(texture))
            {
                ++it;
                continue;
            }
            if (restoreDefaultStates &&
                (!it->second.subresourceStates.empty() || it->second.state != texture->m_desc.defaultState))
                setTextureState(texture, kEntireTexture, texture->m_desc.defaultState);
            it = m_textureStates.erase(it);
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
