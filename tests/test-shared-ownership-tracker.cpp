// Host-side (no-GPU) unit tests for the debug layer's shared-resource ownership validation. The GPU
// interop tests (buffer-shared-cuda / texture-shared-cuda) only exercise the single-resource, in-
// order happy path, and the shared-test harness fails a test only on a debug-layer *error* (a warning
// is merely logged), so the error/warning transitions and the argument-validation front door cannot
// be proven from GPU CI alone. These tests drive the tracker state machine, the CommandList record
// guard, the setBinding/collectSharedBindings path, and the encoder's argument validation directly.

#include "testing.h"
#include "../src/debug-layer/debug-helper-functions.h"
#include "../src/debug-layer/debug-command-encoder.h"
#include "../src/command-list.h"

#include <atomic>
#include <cstdint>
#include <set>
#include <vector>

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

// Minimal COM test doubles for the setBinding/collectSharedBindings path. They exist only for the
// lifetime of a test, so reference counting is a no-op (addRef/release return 2, per the shader-cache
// test pattern) and unused methods return a not-implemented default.

struct StubBuffer : public IBuffer
{
    BufferDesc m_desc;
    // When set, getSharedHandle succeeds and returns this - modeling a producer whose handle is
    // exported (and cached) on the resource. Left unset by tests that resolve via tieImportedResource.
    NativeHandle m_sharedHandle{};
    explicit StubBuffer(BufferUsage usage) { m_desc.usage = usage; }
    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(const SlangUUID& uuid, void** outObject) override
    {
        if (uuid == ISlangUnknown::getTypeGuid() || uuid == IResource::getTypeGuid() || uuid == IBuffer::getTypeGuid())
        {
            *outObject = static_cast<IBuffer*>(this);
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
    }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 2; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 2; }
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle*) override
    {
        return SLANG_E_NOT_IMPLEMENTED;
    }
    virtual SLANG_NO_THROW const BufferDesc& SLANG_MCALL getDesc() override { return m_desc; }
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override
    {
        if (!m_sharedHandle)
            return SLANG_E_NOT_IMPLEMENTED;
        *outHandle = m_sharedHandle;
        return SLANG_OK;
    }
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override { return 0; }
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(
        DescriptorHandleAccess,
        Format,
        BufferRange,
        DescriptorHandle*
    ) override
    {
        return SLANG_E_NOT_IMPLEMENTED;
    }
};

struct StubTexture : public ITexture
{
    TextureDesc m_desc;
    explicit StubTexture(TextureUsage usage) { m_desc.usage = usage; }
    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(const SlangUUID& uuid, void** outObject) override
    {
        if (uuid == ISlangUnknown::getTypeGuid() || uuid == IResource::getTypeGuid() || uuid == ITexture::getTypeGuid())
        {
            *outObject = static_cast<ITexture*>(this);
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
    }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 2; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 2; }
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle*) override
    {
        return SLANG_E_NOT_IMPLEMENTED;
    }
    virtual SLANG_NO_THROW const TextureDesc& SLANG_MCALL getDesc() override { return m_desc; }
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle*) override
    {
        return SLANG_E_NOT_IMPLEMENTED;
    }
    virtual SLANG_NO_THROW Result SLANG_MCALL createView(const TextureViewDesc&, ITextureView**) override
    {
        return SLANG_E_NOT_IMPLEMENTED;
    }
    virtual SLANG_NO_THROW Result SLANG_MCALL getDefaultView(ITextureView**) override
    {
        return SLANG_E_NOT_IMPLEMENTED;
    }
    virtual SLANG_NO_THROW Result SLANG_MCALL getSubresourceLayout(uint32_t, size_t, SubresourceLayout*) override
    {
        return SLANG_E_NOT_IMPLEMENTED;
    }
};

struct StubTextureView : public ITextureView
{
    TextureViewDesc m_desc;
    ITexture* m_texture;
    explicit StubTextureView(ITexture* texture)
        : m_texture(texture)
    {
    }
    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(const SlangUUID& uuid, void** outObject) override
    {
        if (uuid == ISlangUnknown::getTypeGuid() || uuid == IResource::getTypeGuid() ||
            uuid == ITextureView::getTypeGuid())
        {
            *outObject = static_cast<ITextureView*>(this);
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
    }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 2; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 2; }
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle*) override
    {
        return SLANG_E_NOT_IMPLEMENTED;
    }
    virtual SLANG_NO_THROW const TextureViewDesc& SLANG_MCALL getDesc() override { return m_desc; }
    virtual SLANG_NO_THROW ITexture* SLANG_MCALL getTexture() override { return m_texture; }
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(DescriptorHandleAccess, DescriptorHandle*) override
    {
        return SLANG_E_NOT_IMPLEMENTED;
    }
    virtual SLANG_NO_THROW Result SLANG_MCALL getCombinedTextureSamplerDescriptorHandle(DescriptorHandle*) override
    {
        return SLANG_E_NOT_IMPLEMENTED;
    }
};

// Backs a DebugShaderObject so its setBinding/setObject can be driven directly. m_result controls
// whether the base accepts a binding, so the "record only after success" behavior can be tested.
struct StubShaderObject : public IShaderObject
{
    Result m_result = SLANG_OK;
    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(const SlangUUID& uuid, void** outObject) override
    {
        if (uuid == ISlangUnknown::getTypeGuid() || uuid == IShaderObject::getTypeGuid())
        {
            *outObject = static_cast<IShaderObject*>(this);
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
    }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 2; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 2; }
    virtual SLANG_NO_THROW slang::TypeLayoutReflection* SLANG_MCALL getElementTypeLayout() override { return nullptr; }
    virtual SLANG_NO_THROW ShaderObjectContainerType SLANG_MCALL getContainerType() override
    {
        return ShaderObjectContainerType::None;
    }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL getEntryPointCount() override { return 0; }
    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(uint32_t, IShaderObject**) override
    {
        return SLANG_E_NOT_IMPLEMENTED;
    }
    virtual SLANG_NO_THROW Result SLANG_MCALL setData(const ShaderOffset&, const void*, Size) override
    {
        return SLANG_OK;
    }
    virtual SLANG_NO_THROW Result SLANG_MCALL getObject(const ShaderOffset&, IShaderObject** outObject) override
    {
        *outObject = nullptr;
        return SLANG_OK;
    }
    virtual SLANG_NO_THROW Result SLANG_MCALL setObject(const ShaderOffset&, IShaderObject*) override
    {
        return m_result;
    }
    virtual SLANG_NO_THROW Result SLANG_MCALL setBinding(const ShaderOffset&, const Binding&) override
    {
        return m_result;
    }
    virtual SLANG_NO_THROW Result SLANG_MCALL setDescriptorHandle(const ShaderOffset&, const DescriptorHandle&) override
    {
        return SLANG_OK;
    }
    virtual SLANG_NO_THROW Result SLANG_MCALL reserveData(const ShaderOffset&, Size, void**) override
    {
        return SLANG_E_NOT_IMPLEMENTED;
    }
    virtual SLANG_NO_THROW Result SLANG_MCALL setSpecializationArgs(
        const ShaderOffset&,
        const slang::SpecializationArg*,
        uint32_t
    ) override
    {
        return SLANG_OK;
    }
    virtual SLANG_NO_THROW const void* SLANG_MCALL getRawData() override { return nullptr; }
    virtual SLANG_NO_THROW Size SLANG_MCALL getSize() override { return 0; }
    virtual SLANG_NO_THROW Result SLANG_MCALL setConstantBufferOverride(IBuffer*) override { return SLANG_OK; }
    virtual SLANG_NO_THROW Result SLANG_MCALL finalize() override { return SLANG_OK; }
    virtual SLANG_NO_THROW bool SLANG_MCALL isFinalized() override { return false; }
};

ShaderOffset offsetAt(uint32_t bindingRangeIndex)
{
    ShaderOffset offset = {};
    offset.bindingRangeIndex = bindingRangeIndex;
    return offset;
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

    SUBCASE("handing off a resource owned by another queue is an error")
    {
        IResource* res = fakeResource(0x2008);
        tracker.tieImportedResource(res, freshHandle());
        tracker.checkUse(&ctx, res, qProducer); // first use acquires ownership for qProducer
        cb.reset();
        tracker.handOff(&ctx, res, qConsumer, qOther); // qConsumer does not own it
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

    SUBCASE("taking over by the wrong queue is an error")
    {
        IResource* res = fakeResource(0x2004);
        tracker.tieImportedResource(res, freshHandle());
        tracker.handOff(&ctx, res, qProducer, qConsumer);
        cb.reset();
        // qOther is not the queue the resource was handed off to (qConsumer).
        tracker.takeOver(&ctx, res, qOther, qProducer);
        CHECK_EQ(cb.messageCount, 1);
        CHECK_EQ(cb.lastType, DebugMessageType::Error);
    }

    SUBCASE("taking over citing the wrong source queue is an error")
    {
        IResource* res = fakeResource(0x2009);
        tracker.tieImportedResource(res, freshHandle());
        tracker.handOff(&ctx, res, qProducer, qConsumer);
        cb.reset();
        // The correct taker (qConsumer) takes over, but names qOther as the source rather than the
        // queue that actually handed off (qProducer).
        tracker.takeOver(&ctx, res, qConsumer, qOther);
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

    SUBCASE("using a resource owned by another queue (no hand-off) is an error on Vulkan")
    {
        IResource* res = fakeResource(0x2007);
        tracker.tieImportedResource(res, freshHandle());
        tracker.checkUse(&ctx, res, qProducer); // acquires for qProducer
        cb.reset();
        // A different queue uses it without an intervening hand-off. On Vulkan the queue-family
        // ownership transfer is real, so this is genuine misuse and an error.
        tracker.checkUse(&ctx, res, qConsumer);
        CHECK_EQ(cb.messageCount, 1);
        CHECK_EQ(cb.lastType, DebugMessageType::Error);
    }

    SUBCASE("using a resource owned by another queue (no hand-off) is a warning on a no-op backend")
    {
        DebugContext d3dCtx;
        d3dCtx.deviceType = DeviceType::D3D12; // a backend where the transfer calls are no-ops
        d3dCtx.debugCallback = &cb;
        IResource* res = fakeResource(0x200a);
        tracker.tieImportedResource(res, freshHandle());
        tracker.checkUse(&d3dCtx, res, qProducer); // acquires for qProducer
        cb.reset();
        // On a no-op backend cross-queue use cannot be attributed to the API, so it is only a warning.
        tracker.checkUse(&d3dCtx, res, qConsumer);
        CHECK_EQ(cb.messageCount, 1);
        CHECK_EQ(cb.lastType, DebugMessageType::Warning);
    }

    SUBCASE("a same-encoder hand-off reclaim restores the owner without a take-over error")
    {
        IResource* res = fakeResource(0x200b);
        tracker.tieImportedResource(res, freshHandle());
        tracker.handOff(&ctx, res, qProducer, qConsumer); // qProducer hands off to qConsumer (silent)
        cb.reset();
        // The same encoder (owned by qProducer) reverses the hand-off. A generic takeOver by qProducer
        // of a qConsumer-bound hand-off would be an error (wrong taker); the reconcile is silent and
        // restores qProducer's ownership.
        tracker.reclaimInSameEncoder(res, qProducer);
        CHECK_EQ(cb.messageCount, 0);
        tracker.checkUse(&ctx, res, qProducer); // qProducer still owns it -> silent
        CHECK_EQ(cb.messageCount, 0);
    }

    SUBCASE("hand-off and take-over match across DebugContexts that share a handle")
    {
        // The tracker is process-global and keyed by the shared NativeHandle, so a hand-off recorded
        // through one device's DebugContext is matched by a take-over through another's: the two sides
        // of a cross-device interop see the same ownership entry via the same handle, even though each
        // wraps the resource with a distinct IResource*.
        RecordingCallback cbA;
        RecordingCallback cbB;
        DebugContext ctxA;
        ctxA.deviceType = DeviceType::Vulkan;
        ctxA.debugCallback = &cbA;
        DebugContext ctxB;
        ctxB.deviceType = DeviceType::Vulkan;
        ctxB.debugCallback = &cbB;

        NativeHandle handle = freshHandle();
        IResource* resA = fakeResource(0x2100);
        IResource* resB = fakeResource(0x2101);
        tracker.tieImportedResource(resA, handle);
        tracker.tieImportedResource(resB, handle);

        tracker.handOff(&ctxA, resA, qProducer, qConsumer);
        tracker.takeOver(&ctxB, resB, qConsumer, qProducer);
        CHECK_EQ(cbA.messageCount, 0);
        CHECK_EQ(cbB.messageCount, 0);
    }
}

// Regression test for a null resource array on the record-then-replay path: handOffShared /
// takeOverShared record a POD command whose backend recorder indexes `resources` directly, so an
// inconsistent {resourceCount > 0, resources == nullptr} must never be recorded (it would dereference
// null during replay). CommandList::write normalizes the count to zero in that case. This exercises
// that guard without a device.
TEST_CASE("shared-transfer-null-resource-array")
{
    ArenaAllocator allocator;
    std::set<RefPtr<RefObject>> trackedObjects;
    std::vector<ExecuteCallbackObjectRetainer> trackedExecuteCallbackObjects;
    CommandList commandList(allocator, trackedObjects, trackedExecuteCallbackObjects);

    SUBCASE("handOffShared with a null array records a zero count")
    {
        commands::HandOffShared cmd;
        cmd.resourceCount = 2;
        cmd.resources = nullptr;
        cmd.destQueue = nullptr;
        commandList.write(std::move(cmd));

        const CommandList::CommandSlot* slot = commandList.getCommands();
        REQUIRE(slot != nullptr);
        CHECK_EQ(commandList.getCommand<commands::HandOffShared>(slot).resourceCount, 0u);
    }

    SUBCASE("takeOverShared with a null array records a zero count")
    {
        commands::TakeOverShared cmd;
        cmd.resourceCount = 3;
        cmd.resources = nullptr;
        cmd.srcQueue = nullptr;
        commandList.write(std::move(cmd));

        const CommandList::CommandSlot* slot = commandList.getCommands();
        REQUIRE(slot != nullptr);
        CHECK_EQ(commandList.getCommand<commands::TakeOverShared>(slot).resourceCount, 0u);
    }
}

// Exercises the debug-layer setBinding -> draw/dispatch shared-resource validation directly, via
// DebugShaderObject. The GPU interop tests only run the happy path, and the test harness fails only on
// a debug-layer *error* (a warning just logs), so this host test is the only place that asserts the
// validation fires on misuse and stays silent on correct use, and it covers the buffer / texture-view
// / counter / rebind / rejected-binding recording cases the dispatch check depends on.
TEST_CASE("shared-binding-validation")
{
    // ctx and cb outlive the per-subcase objects below. Each subcase declares its stub resources
    // first and the debug objects (which retain those resources) last, so the debug objects are
    // destroyed first and never release an already-destroyed stub.
    DebugContext ctx;
    ctx.deviceType = DeviceType::Vulkan;
    RecordingCallback cb;
    ctx.debugCallback = &cb;
    auto& tracker = SharedResourceOwnershipTracker::get();

    SUBCASE("a Shared buffer binding is recorded, and validation fires on the wrong queue")
    {
        StubShaderObject base;
        StubBuffer buffer(BufferUsage::Shared);
        RefPtr<DebugRootShaderObject> root = new DebugRootShaderObject(&ctx);
        root->baseObject = &base;

        tracker.tieImportedResource(&buffer, freshHandle());
        REQUIRE(SLANG_SUCCEEDED(root->setBinding(offsetAt(0), Binding(&buffer))));
        std::vector<IResource*> shared;
        root->collectSharedBindings(shared);
        REQUIRE_EQ(shared.size(), size_t(1));
        CHECK_EQ(shared[0], static_cast<IResource*>(&buffer));

        ICommandQueue* qOwner = fakeQueue(0x3001);
        ICommandQueue* qOther = fakeQueue(0x3002);
        cb.reset();
        tracker.checkUse(&ctx, &buffer, qOwner); // first use acquires ownership for qOwner
        tracker.checkUse(&ctx, &buffer, qOwner); // reuse by the owner is silent
        CHECK_EQ(cb.messageCount, 0);
        tracker.checkUse(&ctx, &buffer, qOther); // a different queue on Vulkan is an error
        CHECK_EQ(cb.messageCount, 1);
        CHECK_EQ(cb.lastType, DebugMessageType::Error);
    }

    SUBCASE("a non-Shared buffer binding is not recorded")
    {
        StubShaderObject base;
        StubBuffer buffer(BufferUsage::ShaderResource);
        RefPtr<DebugRootShaderObject> root = new DebugRootShaderObject(&ctx);
        root->baseObject = &base;

        REQUIRE(SLANG_SUCCEEDED(root->setBinding(offsetAt(0), Binding(&buffer))));
        std::vector<IResource*> shared;
        root->collectSharedBindings(shared);
        CHECK(shared.empty());
    }

    SUBCASE("a texture binding is resolved through its view to the owning Shared texture")
    {
        StubShaderObject base;
        StubTexture texture(TextureUsage::Shared);
        StubTextureView view(&texture);
        RefPtr<DebugRootShaderObject> root = new DebugRootShaderObject(&ctx);
        root->baseObject = &base;

        REQUIRE(SLANG_SUCCEEDED(root->setBinding(offsetAt(0), Binding(&view))));
        std::vector<IResource*> shared;
        root->collectSharedBindings(shared);
        REQUIRE_EQ(shared.size(), size_t(1));
        CHECK_EQ(shared[0], static_cast<IResource*>(&texture));
    }

    SUBCASE("both a buffer and its Shared counter are recorded")
    {
        StubShaderObject base;
        StubBuffer buffer(BufferUsage::Shared);
        StubBuffer counter(BufferUsage::Shared);
        RefPtr<DebugRootShaderObject> root = new DebugRootShaderObject(&ctx);
        root->baseObject = &base;

        REQUIRE(SLANG_SUCCEEDED(root->setBinding(offsetAt(0), Binding(&buffer, &counter))));
        std::vector<IResource*> shared;
        root->collectSharedBindings(shared);
        CHECK_EQ(shared.size(), size_t(2));
    }

    SUBCASE("rebinding a slot with no Shared operand clears its entry")
    {
        StubShaderObject base;
        StubBuffer sharedBuffer(BufferUsage::Shared);
        StubBuffer plainBuffer(BufferUsage::ShaderResource);
        RefPtr<DebugRootShaderObject> root = new DebugRootShaderObject(&ctx);
        root->baseObject = &base;

        REQUIRE(SLANG_SUCCEEDED(root->setBinding(offsetAt(0), Binding(&sharedBuffer))));
        REQUIRE(SLANG_SUCCEEDED(root->setBinding(offsetAt(0), Binding(&plainBuffer))));
        std::vector<IResource*> shared;
        root->collectSharedBindings(shared);
        CHECK(shared.empty());
    }

    SUBCASE("a rejected setBinding is not recorded")
    {
        StubShaderObject base;
        StubBuffer buffer(BufferUsage::Shared);
        RefPtr<DebugRootShaderObject> root = new DebugRootShaderObject(&ctx);
        root->baseObject = &base;
        base.m_result = SLANG_E_INVALID_ARG;

        CHECK(SLANG_FAILED(root->setBinding(offsetAt(0), Binding(&buffer))));
        std::vector<IResource*> shared;
        root->collectSharedBindings(shared);
        CHECK(shared.empty());
    }

    SUBCASE("a child object's Shared bindings are collected once the base accepts the child")
    {
        StubShaderObject base;
        StubShaderObject childBase;
        StubBuffer buffer(BufferUsage::Shared);
        RefPtr<DebugRootShaderObject> root = new DebugRootShaderObject(&ctx);
        root->baseObject = &base;
        RefPtr<DebugShaderObject> child = new DebugShaderObject(&ctx);
        child->baseObject = &childBase;

        REQUIRE(SLANG_SUCCEEDED(child->setBinding(offsetAt(0), Binding(&buffer))));
        REQUIRE(SLANG_SUCCEEDED(root->setObject(offsetAt(1), child.get())));
        std::vector<IResource*> shared;
        root->collectSharedBindings(shared);
        REQUIRE_EQ(shared.size(), size_t(1));
        CHECK_EQ(shared[0], static_cast<IResource*>(&buffer));
    }

    SUBCASE("a rejected setObject does not register the child")
    {
        StubShaderObject base;
        StubShaderObject childBase;
        StubBuffer buffer(BufferUsage::Shared);
        RefPtr<DebugRootShaderObject> root = new DebugRootShaderObject(&ctx);
        root->baseObject = &base;
        RefPtr<DebugShaderObject> child = new DebugShaderObject(&ctx);
        child->baseObject = &childBase;

        REQUIRE(SLANG_SUCCEEDED(child->setBinding(offsetAt(0), Binding(&buffer))));
        base.m_result = SLANG_E_INVALID_ARG;
        CHECK(SLANG_FAILED(root->setObject(offsetAt(1), child.get())));
        std::vector<IResource*> shared;
        root->collectSharedBindings(shared);
        CHECK(shared.empty());
    }
}

// Exercises the DebugCommandEncoder::handOffShared/takeOverShared argument-validation front door -
// the checks that run before the base encoder records anything: a null resource array with a nonzero
// count, a null destination/source queue, a non-shared resource, the all-or-nothing batch guard, and
// (on Vulkan) a shared texture whose default state does not map to the general layout the transfer
// requires. Each of these returns SLANG_E_INVALID_ARG before touching the (absent) base encoder, so
// no device is needed. This is the single-encoder argument-validation path, distinct from the
// pass-encoder draw/dispatch validation in shared-binding-validation above.
TEST_CASE("shared-transfer-argument-validation")
{
    DebugContext ctx;
    ctx.deviceType = DeviceType::Vulkan;
    RecordingCallback cb;
    ctx.debugCallback = &cb;
    auto& tracker = SharedResourceOwnershipTracker::get();

    RefPtr<DebugCommandEncoder> encoder = new DebugCommandEncoder(&ctx);
    ICommandQueue* destQueue = fakeQueue(0x4001);

    SUBCASE("a null resource array with a nonzero count is rejected")
    {
        cb.reset();
        CHECK_EQ(encoder->handOffShared(1, nullptr, destQueue), SLANG_E_INVALID_ARG);
        CHECK_EQ(cb.lastType, DebugMessageType::Error);
    }

    SUBCASE("a null destination queue is rejected")
    {
        cb.reset();
        CHECK_EQ(encoder->handOffShared(0, nullptr, nullptr), SLANG_E_INVALID_ARG);
        CHECK_EQ(cb.lastType, DebugMessageType::Error);
    }

    SUBCASE("a null source queue is rejected by takeOverShared")
    {
        cb.reset();
        CHECK_EQ(encoder->takeOverShared(0, nullptr, nullptr), SLANG_E_INVALID_ARG);
        CHECK_EQ(cb.lastType, DebugMessageType::Error);
    }

    SUBCASE("a non-shared resource is rejected")
    {
        StubBuffer plain(BufferUsage::ShaderResource);
        IResource* resources[] = {&plain};
        cb.reset();
        CHECK_EQ(encoder->handOffShared(1, resources, destQueue), SLANG_E_INVALID_ARG);
        CHECK_EQ(cb.lastType, DebugMessageType::Error);
    }

    SUBCASE("a bad operand rejects the whole batch without applying any hand-off")
    {
        StubBuffer good(BufferUsage::Shared);
        StubBuffer bad(BufferUsage::ShaderResource);
        tracker.tieImportedResource(&good, freshHandle());
        IResource* resources[] = {&good, &bad};
        cb.reset();
        CHECK_EQ(encoder->handOffShared(2, resources, destQueue), SLANG_E_INVALID_ARG);
        // The valid operand must not have been handed off: a first use still acquires it silently. A
        // spurious hand-off would instead make this checkUse a use-after-hand-off error.
        cb.reset();
        tracker.checkUse(&ctx, &good, destQueue);
        CHECK_EQ(cb.messageCount, 0);
    }

    SUBCASE("a shared texture with a non-general default state is rejected on Vulkan")
    {
        StubTexture texture(TextureUsage::Shared);
        texture.m_desc.defaultState = ResourceState::ShaderResource;
        tracker.tieImportedResource(&texture, freshHandle());
        IResource* resources[] = {&texture};
        cb.reset();
        CHECK_EQ(encoder->handOffShared(1, resources, destQueue), SLANG_E_INVALID_ARG);
        CHECK_EQ(cb.lastType, DebugMessageType::Error);
    }
}

// A producer resolves its key fresh from getSharedHandle on every lookup and is not pointer-cached,
// so the same IResource* later reporting a different shared handle - or dropping the Shared flag, as a
// recycled address holding a non-shared resource would - resolves to its current handle (or to
// nothing) rather than a stale key. Both cases stay silent; a pointer cache would instead raise a
// cross-queue diagnostic.
TEST_CASE("shared-ownership-tracker-producer-revalidation")
{
    auto& tracker = SharedResourceOwnershipTracker::get();
    RecordingCallback cb;
    DebugContext ctx;
    ctx.deviceType = DeviceType::Vulkan;
    ctx.debugCallback = &cb;
    ICommandQueue* qA = fakeQueue(0x5001);
    ICommandQueue* qB = fakeQueue(0x5002);

    SUBCASE("a producer whose shared handle changes resolves fresh, not to a stale key")
    {
        StubBuffer buf(BufferUsage::Shared);
        buf.m_sharedHandle = freshHandle();
        tracker.checkUse(&ctx, &buf, qA); // first use resolves H1, acquires for qA
        cb.reset();
        buf.m_sharedHandle = freshHandle(); // same pointer now reports a different shared handle
        tracker.checkUse(&ctx, &buf, qB);   // resolves H2 fresh -> first use for qB, silent
        CHECK_EQ(cb.messageCount, 0);
        // Confirm tracking is genuinely active and H2 is owned by qB (not silently disabled): a third
        // queue using it is a cross-queue error.
        ICommandQueue* qC = fakeQueue(0x5003);
        tracker.checkUse(&ctx, &buf, qC);
        CHECK_EQ(cb.messageCount, 1);
        CHECK_EQ(cb.lastType, DebugMessageType::Error);
    }

    SUBCASE("a non-shared resource recycling a producer address is not tracked")
    {
        StubBuffer buf(BufferUsage::Shared);
        buf.m_sharedHandle = freshHandle();
        tracker.checkUse(&ctx, &buf, qA); // acquires for qA
        cb.reset();
        buf.m_desc.usage = BufferUsage::ShaderResource; // recycled by a non-shared resource
        tracker.checkUse(&ctx, &buf, qB); // getSharedHandleOf fails the Shared check -> not tracked -> silent
        CHECK_EQ(cb.messageCount, 0);
    }
}
