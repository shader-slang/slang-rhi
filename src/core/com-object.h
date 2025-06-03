#pragma once

#include "assert.h"
#include "smart-pointer.h"

#include <slang.h>

#include <atomic>

namespace rhi {

/// A base class for COM interfaces that require atomic ref counting
/// and are *NOT* derived from RefObject
class ComBaseObject
{
public:
    /// If assigned the the ref count is *NOT* copied
    ComBaseObject& operator=(const ComBaseObject&) { return *this; }

    /// Copy Ctor, does not copy ref count
    ComBaseObject(const ComBaseObject&)
        : m_refCount(0)
    {
    }

    /// Default Ctor sets with no refs
    ComBaseObject()
        : m_refCount(0)
    {
    }

    /// Dtor needs to be virtual to avoid needing to
    /// Implement release for all derived types.
    virtual ~ComBaseObject() {}

protected:
    inline uint32_t _releaseImpl();

    std::atomic<uint32_t> m_refCount;
};

// ------------------------------------------------------------------
inline uint32_t ComBaseObject::_releaseImpl()
{
    // Check there is a ref count to avoid underflow
    SLANG_RHI_ASSERT(m_refCount != 0);
    const uint32_t count = --m_refCount;
    if (count == 0)
    {
        delete this;
    }
    return count;
}

#define SLANG_COM_BASE_IUNKNOWN_QUERY_INTERFACE                                                                        \
    SLANG_NO_THROW Result SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override                 \
    {                                                                                                                  \
        void* intf = getInterface(uuid);                                                                               \
        if (intf)                                                                                                      \
        {                                                                                                              \
            ++m_refCount;                                                                                              \
            *outObject = intf;                                                                                         \
            return SLANG_OK;                                                                                           \
        }                                                                                                              \
        return SLANG_E_NO_INTERFACE;                                                                                   \
    }
#define SLANG_COM_BASE_IUNKNOWN_ADD_REF                                                                                \
    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override                                                              \
    {                                                                                                                  \
        return ++m_refCount;                                                                                           \
    }
#define SLANG_COM_BASE_IUNKNOWN_RELEASE                                                                                \
    SLANG_NO_THROW uint32_t SLANG_MCALL release() override                                                             \
    {                                                                                                                  \
        return _releaseImpl();                                                                                         \
    }
#define SLANG_COM_BASE_IUNKNOWN_ALL                                                                                    \
    SLANG_COM_BASE_IUNKNOWN_QUERY_INTERFACE                                                                            \
    SLANG_COM_BASE_IUNKNOWN_ADD_REF                                                                                    \
    SLANG_COM_BASE_IUNKNOWN_RELEASE

/// COM object that derives from RefObject
class ComObject : public RefObject
{
public:
    ComObject()
        : RefObject()
    {
    }
    ComObject(const ComObject& rhs)
        : RefObject(rhs)
    {
    }

    ComObject& operator=(const ComObject&) { return *this; }
};

#define SLANG_COM_OBJECT_IUNKNOWN_QUERY_INTERFACE                                                                      \
    SLANG_NO_THROW Result SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override                 \
    {                                                                                                                  \
        void* intf = getInterface(uuid);                                                                               \
        if (intf)                                                                                                      \
        {                                                                                                              \
            addRef();                                                                                                  \
            *outObject = intf;                                                                                         \
            return SLANG_OK;                                                                                           \
        }                                                                                                              \
        return SLANG_E_NO_INTERFACE;                                                                                   \
    }
#define SLANG_COM_OBJECT_IUNKNOWN_ADD_REF                                                                              \
    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override                                                              \
    {                                                                                                                  \
        return addReference();                                                                                         \
    }
#define SLANG_COM_OBJECT_IUNKNOWN_RELEASE                                                                              \
    SLANG_NO_THROW uint32_t SLANG_MCALL release() override                                                             \
    {                                                                                                                  \
        return releaseReference();                                                                                     \
    }
#define SLANG_COM_OBJECT_IUNKNOWN_ALL                                                                                  \
    SLANG_COM_OBJECT_IUNKNOWN_QUERY_INTERFACE                                                                          \
    SLANG_COM_OBJECT_IUNKNOWN_ADD_REF                                                                                  \
    SLANG_COM_OBJECT_IUNKNOWN_RELEASE

} // namespace rhi
