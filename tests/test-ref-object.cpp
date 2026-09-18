#include "testing.h"

#include "core/smart-pointer.h"

#include <atomic>
#include <thread>
#include <vector>

using namespace rhi;

namespace {

struct RefObjectTestState
{
    std::atomic<uint32_t> makeExternalCount = 0;
    std::atomic<uint32_t> makeInternalCount = 0;
    std::atomic<uint32_t> destructorCount = 0;
};

class TestRefObject : public RefObject
{
public:
    TestRefObject(RefObjectTestState* state)
        : m_state(state)
    {
    }

    ~TestRefObject() { ++m_state->destructorCount; }

    void makeExternal() override { ++m_state->makeExternalCount; }
    void makeInternal() override { ++m_state->makeInternalCount; }

private:
    RefObjectTestState* m_state;
};

} // namespace

TEST_CASE("ref-object-dynamic-internal-references")
{
    RefObjectTestState state;
    RefPtr<TestRefObject> external = new TestRefObject(&state);
    TestRefObject* object = external.get();

    CHECK_EQ(object->getReferenceCount(), 1);
    CHECK_EQ(object->getInternalReferenceCount(), 0);

    object->addInternalReference();
    object->addInternalReference();
    CHECK_EQ(object->getReferenceCount(), 3);
    CHECK_EQ(object->getInternalReferenceCount(), 2);
    CHECK_EQ(state.makeInternalCount.load(), 0);

    external.setNull();
    CHECK_EQ(object->getReferenceCount(), 2);
    CHECK_EQ(object->getInternalReferenceCount(), 2);
    CHECK_EQ(state.makeInternalCount.load(), 1);

    object->addReference();
    CHECK_EQ(object->getReferenceCount(), 3);
    CHECK_EQ(object->getInternalReferenceCount(), 2);
    CHECK_EQ(state.makeExternalCount.load(), 1);

    object->releaseReference();
    CHECK_EQ(object->getReferenceCount(), 2);
    CHECK_EQ(object->getInternalReferenceCount(), 2);
    CHECK_EQ(state.makeInternalCount.load(), 2);

    object->releaseInternalReference();
    CHECK_EQ(object->getReferenceCount(), 1);
    CHECK_EQ(object->getInternalReferenceCount(), 1);
    CHECK_EQ(state.destructorCount.load(), 0);

    object->releaseInternalReference();
    CHECK_EQ(state.destructorCount.load(), 1);
}

TEST_CASE("ref-object-set-internal-reference-count")
{
    RefObjectTestState state;
    RefPtr<TestRefObject> external = new TestRefObject(&state);
    RefPtr<TestRefObject> internalOwner = external;
    TestRefObject* object = external.get();

    object->setInternalReferenceCount(1);
    CHECK_EQ(object->getReferenceCount(), 2);
    CHECK_EQ(object->getInternalReferenceCount(), 1);
    CHECK_EQ(state.makeInternalCount.load(), 0);

    external.setNull();
    CHECK_EQ(object->getReferenceCount(), 1);
    CHECK_EQ(object->getInternalReferenceCount(), 1);
    CHECK_EQ(state.makeInternalCount.load(), 1);

    object->setInternalReferenceCount(0);
    CHECK_EQ(object->getReferenceCount(), 1);
    CHECK_EQ(object->getInternalReferenceCount(), 0);
    CHECK_EQ(state.makeExternalCount.load(), 1);

    object->setInternalReferenceCount(1);
    CHECK_EQ(state.makeInternalCount.load(), 2);

    internalOwner.setNull();
    CHECK_EQ(state.destructorCount.load(), 1);
}

TEST_CASE("ref-object-concurrent-internal-references")
{
    constexpr uint32_t kThreadCount = 8;
    constexpr uint32_t kReferencesPerThread = 4096;
    constexpr uint32_t kInternalReferenceCount = kThreadCount * kReferencesPerThread;

    RefObjectTestState state;
    RefPtr<TestRefObject> external = new TestRefObject(&state);
    TestRefObject* object = external.get();

    for (uint32_t i = 0; i < kThreadCount; ++i)
        object->addReference();
    external.setNull();

    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < kThreadCount; ++i)
    {
        threads.emplace_back(
            [object]()
            {
                for (uint32_t j = 0; j < kReferencesPerThread; ++j)
                    object->addInternalReference();
                object->releaseReference();
            }
        );
    }
    for (std::thread& thread : threads)
        thread.join();

    CHECK_EQ(object->getReferenceCount(), kInternalReferenceCount);
    CHECK_EQ(object->getInternalReferenceCount(), kInternalReferenceCount);
    CHECK_EQ(state.makeInternalCount.load(), 1);
    CHECK_EQ(state.makeExternalCount.load(), 0);

    threads.clear();
    for (uint32_t i = 0; i < kThreadCount; ++i)
    {
        threads.emplace_back(
            [object]()
            {
                for (uint32_t j = 0; j < kReferencesPerThread; ++j)
                    object->releaseInternalReference();
            }
        );
    }
    for (std::thread& thread : threads)
        thread.join();

    CHECK_EQ(state.destructorCount.load(), 1);
}
