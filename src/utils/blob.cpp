#include "blob.h"

namespace rhi {

// ISlangCastable
void* BlobBase::castAs(const SlangUUID& guid)
{
    if (auto intf = getInterface(guid))
    {
        return intf;
    }
    return getObject(guid);
}

ISlangUnknown* BlobBase::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == ISlangBlob::getTypeGuid())
    {
        return static_cast<ISlangBlob*>(this);
    }
    if (guid == ISlangCastable::getTypeGuid())
    {
        return static_cast<ISlangCastable*>(this);
    }
    return nullptr;
}

void* BlobBase::getObject(const Guid& guid)
{
    SLANG_UNUSED(guid);
    return nullptr;
}

} // namespace rhi
