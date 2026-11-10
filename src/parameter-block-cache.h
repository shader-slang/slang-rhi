#pragma once

#include "shader-object.h"

#include <map>
#include <unordered_map>

namespace rhi {

// A collision-free structural description. Native handles, type names, and root
// placement are deliberately excluded. Each recording interns each layout once;
// cache hits never inspect reflection or walk the frozen object graph.
using ParameterBlockLayoutKey = std::vector<uint64_t>;

inline void appendOrdinaryLayoutKey(ParameterBlockLayoutKey& key, slang::TypeLayoutReflection* layout)
{
    key.push_back(layout != nullptr);
    if (!layout)
        return;
    key.insert(
        key.end(),
        {uint64_t(layout->getKind()),
         layout->getSize(),
         uint64_t(layout->getAlignment()),
         uint64_t(layout->getFieldCount())}
    );
    for (unsigned i = 0; i < layout->getFieldCount(); ++i)
    {
        auto field = layout->getFieldByIndex(i);
        key.push_back(field->getOffset());
        appendOrdinaryLayoutKey(key, field->getTypeLayout());
    }
    if (layout->getKind() == slang::TypeReflection::Kind::Array)
    {
        key.push_back(layout->getElementCount());
        key.push_back(layout->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM));
        appendOrdinaryLayoutKey(key, layout->getElementTypeLayout());
    }
}

class ParameterBlockLayoutCache
{
public:
    size_t intern(ParameterBlockLayoutKey key)
    {
        return m_keys.emplace(std::move(key), m_keys.size() + 1).first->second;
    }

    size_t get(ShaderObjectLayout* layout)
    {
        if (!layout)
            return 0;
        auto found = m_layouts.find(layout);
        if (found != m_layouts.end())
            return found->second.identity;
        if (testing::gParameterBlockCacheStats)
            ++testing::gParameterBlockCacheStats->layoutDescriptions;
        ParameterBlockLayoutKey key;
        appendOrdinaryLayoutKey(key, layout->getElementTypeLayout());
        key.insert(
            key.end(),
            {layout->getUniformBufferSize(),
             layout->getSlotCount(),
             layout->getSubObjectCount(),
             layout->getBindingRangeCount()}
        );
        for (uint32_t i = 0; i < layout->getBindingRangeCount(); ++i)
        {
            const auto& range = layout->getBindingRange(i);
            key.insert(
                key.end(),
                {uint64_t(range.bindingType),
                 range.count,
                 range.slotIndex,
                 range.subObjectIndex,
                 uint64_t(range.isSpecializable),
                 uint64_t(range.isParameterBlock)}
            );
        }
        key.push_back(layout->getSubObjectRangeCount());
        for (uint32_t i = 0; i < layout->getSubObjectRangeCount(); ++i)
        {
            key.push_back(layout->getSubObjectRange(i).bindingRangeIndex);
            key.push_back(get(layout->getSubObjectRangeLayout(i)));
        }
        layout->appendParameterBlockLayoutKey(key);
        size_t identity = intern(std::move(key));
        m_layouts.emplace(layout, Layout{layout, identity});
        return identity;
    }

    void reset()
    {
        m_layouts.clear();
        m_keys.clear();
    }

private:
    struct Layout
    {
        RefPtr<ShaderObjectLayout> layout;
        size_t identity;
    };
    std::unordered_map<ShaderObjectLayout*, Layout> m_layouts;
    std::map<ParameterBlockLayoutKey, size_t> m_keys;
};

template<typename T>
struct ParameterBlockCache
{
    struct Key
    {
        ShaderObject* object;
        size_t layout;
        size_t variant;
        bool operator==(const Key& rhs) const
        {
            return object == rhs.object && layout == rhs.layout && variant == rhs.variant;
        }
    };
    struct Hash
    {
        size_t operator()(const Key& key) const
        {
            return std::hash<ShaderObject*>{}(key.object) ^ (key.layout * 16777619) ^ (key.variant * 2166136261u);
        }
    };
    struct Entry
    {
        InternalRefPtr<ShaderObject> object;
        T data;
    };

    ParameterBlockLayoutCache layouts;
    std::unordered_map<Key, Entry, Hash> entries;
    // Internal instrumentation: successful preparations and subtree reuse, per recording.
    size_t hits = 0;
    size_t preparations = 0;

    Key key(ShaderObject* object, ShaderObjectLayout* layout, size_t variant = 0)
    {
        return {object, layouts.get(layout), variant};
    }

    T* find(const Key& key)
    {
        auto found = entries.find(key);
        if (found == entries.end())
            return nullptr;
        ++hits;
        if (testing::gParameterBlockCacheStats)
            ++testing::gParameterBlockCacheStats->hits;
        return &found->second.data;
    }

    void insert(const Key& key, const T& data)
    {
        entries.emplace(key, Entry{key.object, data});
        ++preparations;
        if (testing::gParameterBlockCacheStats)
            ++testing::gParameterBlockCacheStats->preparations;
    }

    void reset()
    {
        entries.clear();
        layouts.reset();
        hits = preparations = 0;
    }
};

// Roots reference prepared resource-use lists instead of copying large lists at
// every attachment. Native state/residency processing still traverses these on use.
template<typename T>
struct PreparedBindingLink
{
    T* data;
    PreparedBindingLink* next;
};

} // namespace rhi
