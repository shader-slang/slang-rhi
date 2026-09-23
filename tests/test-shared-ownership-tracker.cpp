#include "testing.h"
#include "../src/debug-layer/debug-helper-functions.h"

#include <atomic>
#include <cstdint>

using namespace rhi;
using namespace rhi::debug;

namespace {

// Records the most recent validation message so a test can assert the severity of a tracker
// transition. handleMessage is what SharedResourceOwnershipTracker::report() ultimately calls.
struct RecordingCallback : public IDebugCallback
{
    int messageCount = 0;
    DebugMessageType lastType = DebugMessageType::Info;

    void reset()
    {
        messageCount = 0;
        lastType = DebugMessageType::Info;
    }

    virtual SLANG_NO_THROW void SLANG_MCALL handleMessage(
        DebugMessageType type,
        DebugMessageSource /*source*/,
        const char* /*message*/
    ) override
    {
        ++messageCount;
        lastType = type;
    }
};

// The tracker stores and compares IResource*/ICommandQueue* by identity and never calls methods on
// them (resolveKey hits the tied-key cache, bypassing getSharedHandle), so opaque non-null addresses
// are sufficient synthetic operands.
IResource* fakeResource(uintptr_t id)
{
    return reinterpret_cast<IResource*>(id);
}

ICommandQueue* fakeQueue(uintptr_t id)
{
    return reinterpret_cast<ICommandQueue*>(id);
}

// The tracker is a process-global, never-evicted singleton, so a re-run of this test in the same
// process must not collide with entries left by a prior run. Handing out a fresh handle value on
// every tie guarantees each case operates on a clean, previously-unseen ownership entry.
NativeHandle freshHandle()
{
    static std::atomic<uint64_t> counter{0x100000};
    return NativeHandle{NativeHandleType::Win32, counter.fetch_add(1)};
}

} // namespace

// Exercises the host-testable SharedResourceOwnershipTracker state machine directly (no GPU): the
// GPU interop tests only cover the single-resource, in-order happy path, so the error/warning
// transitions are validated here.
TEST_CASE("shared-ownership-tracker")
{
    auto& tracker = SharedResourceOwnershipTracker::get();
    RecordingCallback cb;
    DebugContext ctx;
    ctx.deviceType = DeviceType::Vulkan;
    ctx.debugCallback = &cb;

    ICommandQueue* qProducer = fakeQueue(0x1001);
    ICommandQueue* qConsumer = fakeQueue(0x1002);
    ICommandQueue* qOther = fakeQueue(0x1003);

    SUBCASE("valid hand-off then take-over is silent")
    {
        IResource* res = fakeResource(0x2001);
        tracker.tieImportedResource(res, freshHandle());
        cb.reset();
        tracker.handOff(&ctx, res, qProducer, qConsumer);
        tracker.takeOver(&ctx, res, qConsumer, qProducer);
        CHECK_EQ(cb.messageCount, 0);
    }

    SUBCASE("handing off an already-handed-off resource is an error")
    {
        IResource* res = fakeResource(0x2002);
        tracker.tieImportedResource(res, freshHandle());
        tracker.handOff(&ctx, res, qProducer, qConsumer);
        cb.reset();
        tracker.handOff(&ctx, res, qProducer, qConsumer);
        CHECK_EQ(cb.messageCount, 1);
        CHECK_EQ(cb.lastType, DebugMessageType::Error);
    }

    SUBCASE("taking over a resource that was not handed off is an error")
    {
        IResource* res = fakeResource(0x2003);
        tracker.tieImportedResource(res, freshHandle());
        cb.reset();
        tracker.takeOver(&ctx, res, qConsumer, qProducer);
        CHECK_EQ(cb.messageCount, 1);
        CHECK_EQ(cb.lastType, DebugMessageType::Error);
    }

    SUBCASE("taking over from the wrong queue is an error")
    {
        IResource* res = fakeResource(0x2004);
        tracker.tieImportedResource(res, freshHandle());
        tracker.handOff(&ctx, res, qProducer, qConsumer);
        cb.reset();
        // qOther is neither the queue that handed off nor the target it was handed off to.
        tracker.takeOver(&ctx, res, qOther, qProducer);
        CHECK_EQ(cb.messageCount, 1);
        CHECK_EQ(cb.lastType, DebugMessageType::Error);
    }

    SUBCASE("using a handed-off resource is an error")
    {
        IResource* res = fakeResource(0x2005);
        tracker.tieImportedResource(res, freshHandle());
        tracker.handOff(&ctx, res, qProducer, qConsumer);
        cb.reset();
        tracker.checkUse(&ctx, res, qProducer);
        CHECK_EQ(cb.messageCount, 1);
        CHECK_EQ(cb.lastType, DebugMessageType::Error);
    }

    SUBCASE("first use acquires ownership; further use by the owner is silent")
    {
        IResource* res = fakeResource(0x2006);
        tracker.tieImportedResource(res, freshHandle());
        cb.reset();
        tracker.checkUse(&ctx, res, qProducer); // first use acquires for qProducer
        tracker.checkUse(&ctx, res, qProducer);
        CHECK_EQ(cb.messageCount, 0);
    }

    SUBCASE("using a resource owned by another queue (no hand-off) is a warning")
    {
        IResource* res = fakeResource(0x2007);
        tracker.tieImportedResource(res, freshHandle());
        tracker.checkUse(&ctx, res, qProducer); // acquires for qProducer
        cb.reset();
        tracker.checkUse(&ctx, res, qConsumer); // different queue, was never handed off
        CHECK_EQ(cb.messageCount, 1);
        CHECK_EQ(cb.lastType, DebugMessageType::Warning);
    }
}
