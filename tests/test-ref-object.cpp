#include "testing.h"
#include "barrier.h"
#include "core/smart-pointer.h"
#include "rhi-shared.h"

#include <atomic>
#include <algorithm>
#include <array>
#include <semaphore>
#include <thread>
#include <vector>

using namespace rhi;

namespace {

struct LifetimeState
{
    std::atomic<bool> ownerAlive{true};
    std::atomic<uint32_t> destroyed{0};
    std::atomic<uint32_t> destroyedAfterOwner{0};
    std::atomic<uint32_t> ownerLookups{0};
};

struct LifetimeOwner : RefObject
{
    LifetimeState& state;
    std::vector<InternalRefPtr<RefObject>> children;
    std::vector<RefObject*> retired;

    explicit LifetimeOwner(LifetimeState& state)
        : state(state)
    {
    }

    ~LifetimeOwner()
    {
        children.clear();
        // Destruction may enqueue more dependencies.
        while (!retired.empty())
        {
            RefObject* object = retired.back();
            retired.pop_back();
            delete object;
        }
        state.ownerAlive = false;
    }
};

struct LifetimeChild : RefObject
{
    LifetimeOwner* const owner;
    LifetimeState& state;

    explicit LifetimeChild(LifetimeOwner* owner)
        : owner(owner)
        , state(owner->state)
    {
    }

    ~LifetimeChild()
    {
        if (!state.ownerAlive)
            ++state.destroyedAfterOwner;
        ++state.destroyed;
    }

protected:
    RefObject* getLifetimeOwner() const noexcept override
    {
        ++state.ownerLookups;
        return owner;
    }
};

struct PlacedResource : LifetimeChild
{
    InternalRefPtr<LifetimeChild> heap;

    PlacedResource(LifetimeOwner* owner, LifetimeChild* heap)
        : LifetimeChild(owner)
        , heap(heap)
    {
    }

    void deleteThis() override { owner->retired.push_back(this); }
};

struct ParentState
{
    std::atomic<bool> alive{true};
    std::atomic<uint32_t> viewsDestroyed{0};
    std::atomic<uint32_t> errors{0};
};

// Only view creation is needed. Leave GPU services unavailable so these tests run
// against the shared lifetime implementation without a backend or driver.
struct TextureLifetimeDevice : Device
{
    using Device::readBuffer;

    LifetimeState& state;
    std::vector<InternalRefPtr<RefObject>> children;

    explicit TextureLifetimeDevice(LifetimeState& state)
        : state(state)
    {
    }

    ~TextureLifetimeDevice()
    {
        children.clear();
        state.ownerAlive = false;
    }

    Result SLANG_MCALL createTextureView(
        ITexture* texture,
        const TextureViewDesc& desc,
        ITextureView** outView
    ) override;

    Result SLANG_MCALL createTexture(const TextureDesc&, const SubresourceData*, ITexture**) override
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    Result SLANG_MCALL createBuffer(const BufferDesc&, const void*, IBuffer**) override
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    Result SLANG_MCALL mapBuffer(IBuffer*, CpuAccessMode, void**) override { return SLANG_E_NOT_AVAILABLE; }
    Result SLANG_MCALL unmapBuffer(IBuffer*) override { return SLANG_E_NOT_AVAILABLE; }
    Result SLANG_MCALL readBuffer(IBuffer*, Offset, Size, void*) override { return SLANG_E_NOT_AVAILABLE; }
    Result SLANG_MCALL createSampler(const SamplerDesc&, ISampler**) override { return SLANG_E_NOT_AVAILABLE; }
    Result SLANG_MCALL createQueryPool(const QueryPoolDesc&, IQueryPool**) override { return SLANG_E_NOT_AVAILABLE; }
    Result SLANG_MCALL getQueue(QueueType, ICommandQueue**) override { return SLANG_E_NOT_AVAILABLE; }
    Result SLANG_MCALL createShaderProgram(const ShaderProgramDesc&, IShaderProgram**, ISlangBlob**) override
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    Result createShaderObjectLayout(slang::ISession*, slang::TypeLayoutReflection*, ShaderObjectLayout**) override
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    Result createRootShaderObjectLayout(slang::IComponentType*, slang::ProgramLayout*, ShaderObjectLayout**) override
    {
        return SLANG_E_NOT_AVAILABLE;
    }
};

struct LifetimeTextureView;

struct LifetimeTexture : Texture
{
    ParentState& parentState;
    std::atomic<bool> pauseRelease{false};
    std::binary_semaphore releaseReached{0};
    std::binary_semaphore resumeRelease{0};

    LifetimeTexture(TextureLifetimeDevice* device, ParentState& state)
        : Texture(device, {})
        , parentState(state)
    {
    }

    ~LifetimeTexture()
    {
        destroyDefaultView();
        parentState.alive = false;
        auto& state = getDevice<TextureLifetimeDevice>()->state;
        if (!state.ownerAlive)
            ++state.destroyedAfterOwner;
    }

    uint32_t releaseInternalReference() override
    {
        // TextureView has already released its local count. Pause before dropping
        // its texture pin to exercise an overlapping consumer interval in production code.
        if (pauseRelease.exchange(false))
        {
            releaseReached.release();
            resumeRelease.acquire();
        }
        return Texture::releaseInternalReference();
    }

    LifetimeTextureView* getCachedView();
};

struct LifetimeTextureView : TextureView
{
    LifetimeTexture* const texture;

    LifetimeTextureView(LifetimeTexture* texture, const TextureViewDesc& desc = {})
        : TextureView(texture, desc)
        , texture(texture)
    {
    }

    ~LifetimeTextureView()
    {
        if (!texture->parentState.alive)
            ++texture->parentState.errors;
        ++texture->parentState.viewsDestroyed;
    }

    ITexture* SLANG_MCALL getTexture() override { return texture; }
};

Result TextureLifetimeDevice::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    returnComPtrCopy(outView, new LifetimeTextureView(static_cast<LifetimeTexture*>(texture), desc));
    return SLANG_OK;
}

LifetimeTextureView* LifetimeTexture::getCachedView()
{
    ComPtr<ITextureView> view;
    REQUIRE(SLANG_SUCCEEDED(getDefaultView(view.writeRef())));
    // The production cache preserves this borrowed pointer while the texture is alive.
    return static_cast<LifetimeTextureView*>(view.get());
}

} // namespace

TEST_CASE("ref-object-coalesced-owner-reference")
{
    LifetimeState state;
    RefPtr<LifetimeOwner> owner = new LifetimeOwner(state);
    InternalRefPtr<LifetimeChild> child = new LifetimeChild(owner);
    CHECK_EQ(owner->getReferenceCount(), 1);
    CHECK_EQ(state.ownerLookups.load(), 0);
    {
        RefPtr<LifetimeChild> first = child;
        CHECK_EQ(owner->getReferenceCount(), 2);
        CHECK_EQ(state.ownerLookups.load(), 1);
        {
            std::vector<RefPtr<LifetimeChild>> copies(1000, first);
            CHECK_EQ(owner->getReferenceCount(), 2);
            CHECK_EQ(child->getExternalReferenceCount(), 1001);
        }
        // Copying and releasing non-final external references must not dispatch the getter.
        CHECK_EQ(state.ownerLookups.load(), 1);
        InternalRefPtr<LifetimeChild> internalCopy = child;
        CHECK_EQ(child->getInternalReferenceCount(), 2);
    }
    CHECK_EQ(owner->getReferenceCount(), 1);
    CHECK_EQ(child->getExternalReferenceCount(), 0);
    CHECK_EQ(state.ownerLookups.load(), 2);
    InternalRefPtr<LifetimeChild> internalCopy = child;
    child.setNull();
    CHECK_EQ(state.destroyed.load(), 0);
    internalCopy.setNull();
    CHECK_EQ(state.destroyed.load(), 1);
    CHECK_EQ(state.ownerLookups.load(), 2);
    owner.setNull();
    CHECK_FALSE(state.ownerAlive);
    CHECK_EQ(state.destroyedAfterOwner.load(), 0);
}

TEST_CASE("ref-object-reentrant-owner-shutdown")
{
    LifetimeState state;
    RefPtr<LifetimeOwner> owner = new LifetimeOwner(state);
    RefPtr<LifetimeChild> child = new LifetimeChild(owner);
    owner->children.push_back(child);
    owner.setNull();
    CHECK(state.ownerAlive);
    // Ends the child's external lifetime, releases the owner, and destroys the child
    // from inside the owner's destructor before the original release has returned.
    child.setNull();
    CHECK_FALSE(state.ownerAlive);
    CHECK_EQ(state.destroyed.load(), 1);
    CHECK_EQ(state.destroyedAfterOwner.load(), 0);
}

TEST_CASE("ref-object-deferred-shared-backing-release-orders")
{
    // Models resource heaps without depending on the unmerged resource-heap API.
    const uint32_t orders[][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    for (auto& order : orders)
    {
        LifetimeState state;
        RefPtr<LifetimeOwner> owner = new LifetimeOwner(state);
        RefPtr<LifetimeChild> heap = new LifetimeChild(owner);
        RefPtr<PlacedResource> first = new PlacedResource(owner, heap);
        RefPtr<PlacedResource> second = new PlacedResource(owner, heap);
        for (uint32_t object : order)
        {
            switch (object)
            {
            case 0:
                first.setNull();
                second.setNull();
                break;
            case 1:
                heap.setNull();
                break;
            case 2:
                owner.setNull();
                break;
            }
        }
        CHECK_FALSE(state.ownerAlive);
        CHECK_EQ(state.destroyed.load(), 3);
        CHECK_EQ(state.destroyedAfterOwner.load(), 0);
    }
}

TEST_CASE("ref-object-concurrent-external-lifetimes")
{
    constexpr uint32_t threadCount = 8;
    constexpr uint32_t iterations = 2000;
    LifetimeState state;
    RefPtr<LifetimeOwner> owner = new LifetimeOwner(state);
    InternalRefPtr<LifetimeChild> child = new LifetimeChild(owner);
    rhi::testing::Barrier phase(threadCount);
    std::atomic<uint32_t> errors{0};
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < threadCount; ++i)
    {
        threads.emplace_back(
            [&, retained = child]()
            {
                for (uint32_t j = 0; j < iterations; ++j)
                {
                    phase.arriveAndWait();
                    RefPtr<LifetimeChild> external = retained;
                    phase.arriveAndWait();
                    if (owner->getReferenceCount() != 2 || child->getExternalReferenceCount() != threadCount)
                        ++errors;
                    phase.arriveAndWait();
                    external.setNull();
                    phase.arriveAndWait();
                    if (owner->getReferenceCount() != 1 || child->getExternalReferenceCount() != 0)
                        ++errors;
                }
            }
        );
    }
    for (auto& thread : threads)
        thread.join();
    CHECK_EQ(errors.load(), 0);
    child.setNull();
    owner.setNull();
    CHECK_EQ(state.destroyed.load(), 1);
    CHECK_EQ(state.destroyedAfterOwner.load(), 0);
}

TEST_CASE("ref-object-concurrent-final-releases")
{
    constexpr uint32_t iterations = 1000;
    LifetimeState state;
    RefPtr<LifetimeOwner> owner = new LifetimeOwner(state);
    LifetimeChild* child = nullptr;
    rhi::testing::Barrier phase(2);
    std::thread internalReleaser(
        [&]
        {
            for (uint32_t i = 0; i < iterations; ++i)
            {
                phase.arriveAndWait();
                child->releaseInternalReference();
                phase.arriveAndWait();
            }
        }
    );
    for (uint32_t i = 0; i < iterations; ++i)
    {
        RefPtr<LifetimeChild> external = new LifetimeChild(owner);
        child = external;
        child->addInternalReference();
        phase.arriveAndWait();
        external.setNull();
        phase.arriveAndWait();
    }
    internalReleaser.join();
    CHECK_EQ(owner->getReferenceCount(), 1);
    CHECK_EQ(state.destroyed.load(), iterations);
    CHECK_EQ(state.destroyedAfterOwner.load(), 0);
}

TEST_CASE("ref-object-overlapping-external-lifetimes")
{
    LifetimeState state;
    RefPtr<LifetimeOwner> owner = new LifetimeOwner(state);
    InternalRefPtr<LifetimeChild> child = new LifetimeChild(owner);
    std::atomic<uint32_t> errors{0};
    rhi::testing::Barrier start(8);
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < 8; ++i)
    {
        threads.emplace_back(
            [&, retained = child]()
            {
                start.arriveAndWait();
                for (uint32_t j = 0; j < 20000; ++j)
                {
                    RefPtr<LifetimeChild> external = retained;
                    // An older external lifetime may still be releasing its pin, so >2 is
                    // valid. Our own external reference must always guarantee at least 2.
                    if (owner->getReferenceCount() < 2)
                        ++errors;
                }
            }
        );
    }
    for (auto& thread : threads)
        thread.join();
    CHECK_EQ(errors.load(), 0);
    CHECK_EQ(owner->getReferenceCount(), 1);
    CHECK_EQ(child->getExternalReferenceCount(), 0);
}

TEST_CASE("ref-object-concurrent-internal-references")
{
    constexpr uint32_t threadCount = 8;
    constexpr uint32_t referencesPerThread = 4096;
    LifetimeState state;
    RefPtr<LifetimeOwner> owner = new LifetimeOwner(state);
    LifetimeChild* child = new LifetimeChild(owner);
    for (uint32_t i = 0; i < threadCount; ++i)
        child->addReference();
    CHECK_EQ(owner->getReferenceCount(), 2);

    rhi::testing::Barrier start(threadCount);
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < threadCount; ++i)
    {
        threads.emplace_back(
            [&]
            {
                start.arriveAndWait();
                for (uint32_t j = 0; j < referencesPerThread; ++j)
                    child->addInternalReference();
                child->releaseReference();
            }
        );
    }
    for (auto& thread : threads)
        thread.join();
    threads.clear();

    CHECK_EQ(child->getExternalReferenceCount(), 0);
    CHECK_EQ(child->getInternalReferenceCount(), threadCount * referencesPerThread);
    CHECK_EQ(owner->getReferenceCount(), 1);
    CHECK_EQ(state.destroyed.load(), 0);

    for (uint32_t i = 0; i < threadCount; ++i)
    {
        threads.emplace_back(
            [&]
            {
                start.arriveAndWait();
                for (uint32_t j = 0; j < referencesPerThread; ++j)
                    child->releaseInternalReference();
            }
        );
    }
    for (auto& thread : threads)
        thread.join();

    CHECK_EQ(state.destroyed.load(), 1);
    CHECK_EQ(owner->getReferenceCount(), 1);
    owner.setNull();
    CHECK_FALSE(state.ownerAlive);
    CHECK_EQ(state.destroyedAfterOwner.load(), 0);
}

TEST_CASE("ref-object-parent-owned-cache-release-orders")
{
    std::array<uint32_t, 4> order = {0, 1, 2, 3};
    do
    {
        LifetimeState state;
        ParentState parentState;
        RefPtr<TextureLifetimeDevice> owner = new TextureLifetimeDevice(state);
        RefPtr<LifetimeTexture> parent = new LifetimeTexture(owner, parentState);
        RefPtr<LifetimeTextureView> cached = parent->getCachedView();
        RefPtr<LifetimeTextureView> ordinary = new LifetimeTextureView(parent);
        CHECK_EQ(parent->getExternalReferenceCount(), 3);
        CHECK_EQ(parent->getInternalReferenceCount(), 0);
        CHECK_EQ(owner->getReferenceCount(), 2);
        for (uint32_t item : order)
        {
            switch (item)
            {
            case 0:
                owner.setNull();
                break;
            case 1:
                parent.setNull();
                break;
            case 2:
                cached.setNull();
                break;
            case 3:
                ordinary.setNull();
                break;
            }
        }
        CHECK_FALSE(state.ownerAlive);
        CHECK_FALSE(parentState.alive);
        CHECK_EQ(parentState.viewsDestroyed.load(), 2);
        CHECK_EQ(parentState.errors.load(), 0);
        CHECK_EQ(state.destroyedAfterOwner.load(), 0);
    }
    while (std::next_permutation(order.begin(), order.end()));
}

TEST_CASE("ref-object-dormant-cache-internal-consumers")
{
    LifetimeState state;
    ParentState parentState;
    RefPtr<TextureLifetimeDevice> owner = new TextureLifetimeDevice(state);
    RefPtr<LifetimeTexture> parent = new LifetimeTexture(owner, parentState);
    auto* view = parent->getCachedView();
    CHECK_EQ(view->getReferenceCount(), 0);
    CHECK_EQ(parent->getInternalReferenceCount(), 0);
    {
        // Exercise the type-erased path used by command/resource tracking.
        InternalRefPtr<RefObject> consumer = view;
        std::vector<InternalRefPtr<RefObject>> copies(1000, consumer);
        CHECK_EQ(parent->getInternalReferenceCount(), 1001);
        CHECK_EQ(owner->getReferenceCount(), 2); // Application + external parent.
    }
    CHECK_EQ(view->getReferenceCount(), 0);
    CHECK_EQ(parent->getInternalReferenceCount(), 0);
    CHECK_EQ(parentState.viewsDestroyed.load(), 0);
    owner->children.emplace_back(view);
    parent.setNull();
    // Device teardown drops the last internal view consumer, then destroys its parent
    // and the dormant cached view while the device's services are still available.
    owner.setNull();
    CHECK_FALSE(state.ownerAlive);
    CHECK_FALSE(parentState.alive);
    CHECK_EQ(parentState.viewsDestroyed.load(), 1);
    CHECK_EQ(state.destroyedAfterOwner.load(), 0);
}

TEST_CASE("ref-object-cache-overlapping-last-and-first-consumers")
{
    for (bool external : {false, true})
    {
        LifetimeState state;
        ParentState parentState;
        RefPtr<TextureLifetimeDevice> owner = new TextureLifetimeDevice(state);
        RefPtr<LifetimeTexture> parent = new LifetimeTexture(owner, parentState);
        auto* view = parent->getCachedView();
        view->addInternalReference();
        parent->pauseRelease = true;
        std::thread releaser(
            [&]
            {
                view->releaseInternalReference();
            }
        );
        parent->releaseReached.acquire(); // Count is zero; the old parent pin is still held.
        CHECK_EQ(view->getReferenceCount(), 0);
        CHECK_EQ(parent->getInternalReferenceCount(), 1);
        if (external)
            view->addReference();
        else
            view->addInternalReference();
        CHECK_EQ(parent->getInternalReferenceCount(), external ? 1 : 2);
        CHECK_EQ(parent->getExternalReferenceCount(), external ? 2 : 1);
        if (external)
            view->releaseReference();
        else
            view->releaseInternalReference();
        CHECK_EQ(parent->getInternalReferenceCount(), 1);
        auto* texture = parent.get();
        parent.setNull(); // Only the old, unfinished release now protects the texture.
        CHECK(parentState.alive);
        texture->resumeRelease.release();
        releaser.join();
        CHECK_FALSE(parentState.alive);
        CHECK_EQ(parentState.viewsDestroyed.load(), 1);
        CHECK_EQ(parentState.errors.load(), 0);
    }
}

TEST_CASE("ref-object-cache-concurrent-consumers")
{
    LifetimeState state;
    ParentState parentState;
    RefPtr<TextureLifetimeDevice> owner = new TextureLifetimeDevice(state);
    RefPtr<LifetimeTexture> parent = new LifetimeTexture(owner, parentState);
    auto* view = parent->getCachedView();
    rhi::testing::Barrier start(8);
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < 8; ++i)
    {
        threads.emplace_back(
            [&, i]
            {
                start.arriveAndWait();
                for (uint32_t j = 0; j < 20000; ++j)
                {
                    if ((i + j) % 2)
                    {
                        RefPtr<RefObject> consumer = view;
                        if (parent->getExternalReferenceCount() < 2 || owner->getReferenceCount() != 2)
                            ++parentState.errors;
                    }
                    else
                    {
                        InternalRefPtr<RefObject> consumer = view;
                        if (parent->getInternalReferenceCount() == 0)
                            ++parentState.errors;
                    }
                }
            }
        );
    }
    for (auto& thread : threads)
        thread.join();
    CHECK_EQ(parentState.errors.load(), 0);
    CHECK_EQ(view->getReferenceCount(), 0);
    CHECK_EQ(parent->getInternalReferenceCount(), 0);
    CHECK_EQ(owner->getReferenceCount(), 2);
}

TEST_CASE("ref-object-cache-concurrent-final-releases")
{
    constexpr uint32_t iterations = 1000;
    LifetimeState state;
    RefPtr<TextureLifetimeDevice> owner = new TextureLifetimeDevice(state);
    LifetimeTextureView* view = nullptr;
    rhi::testing::Barrier phase(2);
    std::thread internalReleaser(
        [&]
        {
            for (uint32_t i = 0; i < iterations; ++i)
            {
                phase.arriveAndWait();
                view->releaseInternalReference();
                phase.arriveAndWait();
            }
        }
    );
    for (uint32_t i = 0; i < iterations; ++i)
    {
        ParentState parentState;
        RefPtr<LifetimeTexture> parent = new LifetimeTexture(owner, parentState);
        RefPtr<LifetimeTextureView> external = parent->getCachedView();
        view = external;
        view->addInternalReference();
        parent.setNull();
        phase.arriveAndWait();
        external.setNull();
        phase.arriveAndWait();
        CHECK_FALSE(parentState.alive);
        CHECK_EQ(parentState.viewsDestroyed.load(), 1);
        CHECK_EQ(parentState.errors.load(), 0);
    }
    internalReleaser.join();
    CHECK_EQ(owner->getReferenceCount(), 1);
    CHECK_EQ(state.destroyedAfterOwner.load(), 0);
}
