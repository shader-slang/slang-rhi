#include "diagnostics.h"

#include "rhi-shared.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace rhi {

thread_local int gDisableNativeCallError;

void formatNativeCallError(
    char* buffer,
    size_t bufferSize,
    const char* call,
    int64_t result,
    const char* resultName,
    const SourceLocation& location,
    const char* detail
)
{
    if (!buffer || bufferSize == 0)
        return;

    if (!call)
        call = "<unknown>";
    if (!resultName)
        resultName = "<unknown>";

    buffer[0] = 0;
    size_t length = 0;
    auto append = [&](const char* format, ...)
    {
        if (length >= bufferSize - 1)
            return;

        va_list args;
        va_start(args, format);
        int written = std::vsnprintf(buffer + length, bufferSize - length, format, args);
        va_end(args);

        if (written <= 0)
            return;

        size_t remaining = bufferSize - length;
        size_t count = static_cast<size_t>(written);
        length += count < remaining ? count : remaining - 1;
    };

    append("%s call failed\n  result: %lld (%s)\n", call, static_cast<long long>(result), resultName);

    if (detail && detail[0])
    {
        size_t detailLength = std::strlen(detail);
        append("  detail: %s%s", detail, detail[detailLength - 1] == '\n' ? "" : "\n");
    }

    if (location.file)
    {
        append("  location: %s:%d\n", location.file, location.line);
    }
}

void reportNativeCallError(
    Device* device,
    const char* call,
    int64_t result,
    const char* resultName,
    const SourceLocation& location,
    const char* detail
)
{
    if (gDisableNativeCallError > 0)
        return;

    char message[4096];
    formatNativeCallError(message, sizeof(message), call, result, resultName, location, detail);

    if (device)
    {
        device->handleMessage(DebugMessageType::Error, DebugMessageSource::Driver, message);
    }
    else
    {
        // If we don't have a device, just print to stderr.
        std::fprintf(stderr, "%s\n", message);
    }
}

ScopedDisableNativeCallError::ScopedDisableNativeCallError()
{
    gDisableNativeCallError++;
}

ScopedDisableNativeCallError::~ScopedDisableNativeCallError()
{
    gDisableNativeCallError--;
}


} // namespace rhi
