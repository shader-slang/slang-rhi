#include "diagnostics.h"

#include "rhi-shared.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace rhi {

thread_local int tls_disableNativeCallError = 0;
static thread_local int tls_nativeCallErrorWarning = 0;

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
    bool truncated = false;
    auto append = [&](const char* format, ...)
    {
        if (length >= bufferSize - 1)
        {
            truncated = true;
            return;
        }

        va_list args;
        va_start(args, format);
        int written = std::vsnprintf(buffer + length, bufferSize - length, format, args);
        va_end(args);

        if (written <= 0)
            return;

        size_t remaining = bufferSize - length;
        size_t count = static_cast<size_t>(written);
        truncated |= count >= remaining;
        length += count < remaining ? count : remaining - 1;
    };

    append("%s call failed\n  result: %lld (%s)\n", call, static_cast<long long>(result), resultName);

    if (location.file)
    {
        append("  location: %s:%d\n", location.file, location.line);
    }

    if (detail && detail[0])
    {
        size_t detailLength = std::strlen(detail);
        append("  detail: %s%s", detail, detail[detailLength - 1] == '\n' ? "" : "\n");
    }

    if (truncated)
    {
        static constexpr char marker[] = "...[truncated]\n";
        if (bufferSize >= sizeof(marker))
            std::memcpy(buffer + bufferSize - sizeof(marker), marker, sizeof(marker));
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
    if (tls_disableNativeCallError > 0)
        return;

    char message[4096];
    formatNativeCallError(message, sizeof(message), call, result, resultName, location, detail);

    DebugMessageType type = tls_nativeCallErrorWarning > 0 ? DebugMessageType::Warning : DebugMessageType::Error;
    if (device && device->m_debugCallback && device->m_debugCallback != NullDebugCallback::getInstance())
    {
        device->handleMessage(type, DebugMessageSource::Driver, message);
    }
    else
    {
        std::fprintf(stderr, "%s: %s\n", type == DebugMessageType::Warning ? "warning" : "error", message);
    }
}

ScopedDisableNativeCallError::ScopedDisableNativeCallError()
{
    tls_disableNativeCallError++;
}

ScopedDisableNativeCallError::~ScopedDisableNativeCallError()
{
    tls_disableNativeCallError--;
}

ScopedNativeCallErrorWarning::ScopedNativeCallErrorWarning()
{
    tls_nativeCallErrorWarning++;
}

ScopedNativeCallErrorWarning::~ScopedNativeCallErrorWarning()
{
    tls_nativeCallErrorWarning--;
}

} // namespace rhi
