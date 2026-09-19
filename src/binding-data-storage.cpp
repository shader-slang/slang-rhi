#include "binding-data-storage.h"
#include "rhi-shared.h"

namespace rhi {

Result BindingDataStorage::writeOrdinaryData(
    ShaderObject* object,
    ShaderObjectLayout* layout,
    Size dataSize,
    Size allocationSize,
    UniformData& outData
)
{
    outData = {};
    if (dataSize > allocationSize)
        return SLANG_E_INVALID_ARG;

    if (object->isFinalized())
    {
        PersistentBufferPool::Allocation* allocation;
        SLANG_RETURN_ON_FAIL(object->getOrdinaryDataAllocation(layout, allocationSize, allocation));
        retain(allocation);
        outData = {allocation->getBuffer(), allocation->getOffset()};
        return SLANG_OK;
    }

    // A persistent record must never capture a mutable object's transient uniform allocation.
    if (isPersistent() || !m_constantBufferArena)
        return SLANG_E_INVALID_ARG;

    TransientBufferArena::Allocation allocation;
    SLANG_RETURN_ON_FAIL(m_constantBufferArena->allocate(allocationSize, &allocation));
    SLANG_RETURN_ON_FAIL(object->writeOrdinaryData(allocation.mappedData, dataSize, layout));
    outData = {allocation.buffer, allocation.offset};
    return SLANG_OK;
}

} // namespace rhi
