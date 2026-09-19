#pragma once

#include "shader-object.h"
#include "transient-buffer-heap.h"

namespace rhi {

/// A recording-time view of storage owned by a command buffer or a prepared shader object.
/// The owner outlives the view and keeps every returned allocation alive. Backend extensions
/// provide native descriptor allocation using the same owner's lifetime.
class BindingDataStorage
{
public:
    BindingDataStorage(const BindingDataStorage&) = delete;
    BindingDataStorage& operator=(const BindingDataStorage&) = delete;

    template<typename T>
    T* allocate(size_t count = 1)
    {
        return m_allocator.allocate<T>(count);
    }

    void* allocate(size_t size) { return m_allocator.allocate(size); }
    void retain(RefObject* object) { m_resources.insert(object); }
    void trackResources(ShaderObject* object) { object->trackResources(m_resources); }
    bool isPersistent() const { return m_persistent; }

    struct UniformData
    {
        Buffer* buffer = nullptr;
        Offset offset = 0;
    };

    /// Reuse finalized uniform storage or write a mutable snapshot to the transient arena.
    /// allocationSize includes any backend-required padding; dataSize is the serialized size.
    Result writeOrdinaryData(
        ShaderObject* object,
        ShaderObjectLayout* layout,
        Size dataSize,
        Size allocationSize,
        UniformData& outData
    );

protected:
    /// Transient storage for backends with their own uniform pools or host parameter data.
    BindingDataStorage(ArenaAllocator& allocator, std::set<RefPtr<RefObject>>& resources)
        : m_allocator(allocator)
        , m_resources(resources)
    {
    }

    BindingDataStorage(
        ArenaAllocator& allocator,
        std::set<RefPtr<RefObject>>& resources,
        TransientBufferArena& constantBufferArena
    )
        : m_allocator(allocator)
        , m_resources(resources)
        , m_constantBufferArena(&constantBufferArena)
    {
    }

    explicit BindingDataStorage(PreparedShaderObject& owner)
        : m_allocator(owner.allocator)
        , m_resources(owner.resources)
        , m_persistent(true)
    {
    }

    ~BindingDataStorage() = default;

private:
    ArenaAllocator& m_allocator;
    std::set<RefPtr<RefObject>>& m_resources;
    TransientBufferArena* m_constantBufferArena = nullptr;
    bool m_persistent = false;
};

} // namespace rhi
