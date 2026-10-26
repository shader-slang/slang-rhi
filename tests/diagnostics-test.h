#pragma once

#include "testing.h"

namespace rhi::testing {

struct DiagnosticCapture : IDebugCallback
{
    struct Message
    {
        DebugMessageType type;
        DebugMessageSource source;
        std::string text;
    };
    std::vector<Message> messages;

    void SLANG_MCALL handleMessage(DebugMessageType type, DebugMessageSource source, const char* message) override
    {
        messages.push_back({type, source, message});
    }
};

// Restore driver entry points even when a test assertion exits early. These tests run serially.
template<typename T>
struct ScopedFunctionOverride
{
    T& target;
    T original;
    ScopedFunctionOverride(T& target, T replacement)
        : target(target)
        , original(target)
    {
        target = replacement;
    }
    ~ScopedFunctionOverride() { target = original; }
};

} // namespace rhi::testing
