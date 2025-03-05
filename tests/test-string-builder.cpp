#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

#include "core/string-builder.h"

TEST_CASE("string-builder")
{
    StringBuilder sb;

    sb << "string\n";
    sb << "int8_t: " << int8_t(-1) << "\n";
    sb << "uint8_t: " << uint8_t(1) << "\n";
    sb << "int16_t: " << int16_t(-2) << "\n";
    sb << "uint16_t: " << uint16_t(2) << "\n";
    sb << "int32_t: " << int32_t(-3) << "\n";
    sb << "uint32_t: " << uint32_t(3) << "\n";
    sb << "int64_t: " << int64_t(-4) << "\n";
    sb << "uint64_t: " << uint64_t(4) << "\n";
    sb << "float: " << 1.0f << "\n";
    sb << "double: " << 2.0 << "\n";
    sb << "formatted: " << Formatted("%d", 3) << "\n";
    printf("%s", sb.c_str());
}

#include "strings.h"
#include "enum-strings.h"

#define BUFFER(buffer) ""
#define TEXTURE(texture) ""
#define ENUM(type) S_ENUM(type)
#define ENUM_VALUE(type, value) S_ENUM(type) << "::" << enumToString(type::value)
#define ARG(arg) ""

#define V_METHOD(cls, method) StringBuilder sb;

#define ERROR sb

#define RETURN_INVALID_ARG return SLANG_E_INVALID_ARG

#define CHECK_NOT_NULL(arg)                                                                                            \
    if (!(arg))                                                                                                        \
    {                                                                                                                  \
    ERROR << ARG(arg) << "must not be null";                                                                       \
        RETURN_INVALID_ARG;                                                                                            \
    }

#define S_CpuAccessMode "CpuAccessMode"

const char* enumToString(CpuAccessMode value) { return ""; }


Result mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData)
{
    V_METHOD(Device, mapBuffer);

    CHECK_NOT_NULL(buffer)

    switch (mode)
    {
    case CpuAccessMode::Read:
        if (buffer->getDesc().memoryType != MemoryType::ReadBack)
        {
            ERROR << BUFFER(buffer) << " must be created with " << ENUM_VALUE(MemoryType, ReadBack) << " to map with "
                  << ARG(mode) << "=" << ENUM_VALUE(CpuAccessMode, Read);
            RETURN_INVALID_ARG;
        }
        break;
    case CpuAccessMode::Write:
        if (buffer->getDesc().memoryType != MemoryType::Upload)
        {
            ERROR << BUFFER(buffer) << " must be created with " << ENUM_VALUE(MemoryType, Upload) << " to map with "
                  << ARG(mode) << "=" << ENUM_VALUE(CpuAccessMode, Write);
            RETURN_INVALID_ARG;
        }
        break;
    default:
        ERROR << "Invalid " << ENUM(CpuAccessMode);
        RETURN_INVALID_ARG;
    }

    return SLANG_OK;
}
