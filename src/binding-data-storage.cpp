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
        Buffer* buffer;
        SLANG_RETURN_ON_FAIL(object->getOrdinaryDataBuffer(layout, allocationSize, buffer));
        retain(buffer);
        outData.buffer = buffer;
        return SLANG_OK;
    }

    // A persistent record must never capture a mutable object's transient uniform allocation.
    if (isPersistent())
        return SLANG_E_INVALID_ARG;

    TransientBufferArena::Allocation allocation;
    SLANG_RETURN_ON_FAIL(m_constantBufferArena->allocate(allocationSize, &allocation));
    SLANG_RETURN_ON_FAIL(object->writeOrdinaryData(allocation.mappedData, dataSize, layout));
    outData = {allocation.buffer, allocation.offset};
    return SLANG_OK;
}

} // namespace rhi
