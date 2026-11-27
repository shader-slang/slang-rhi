// test-cpu-dispatch.slang - Shaders for test-cpu-dispatch.cpp.

// Each thread writes its flattened dispatch thread index + 1 to its own element and counts itself.
uniform RWStructuredBuffer<uint> ids;
uniform RWStructuredBuffer<Atomic<uint>> threadCount;
uniform uint3 dispatchSize; // in threads

[shader("compute")]
[numthreads(4, 2, 1)]
void writeIds(uint3 tid: SV_DispatchThreadID)
{
    uint index = (tid.z * dispatchSize.y + tid.y) * dispatchSize.x + tid.x;
    ids[index] = index + 1;
    threadCount[0].add(1);
}

// The first group waits until the last group of the dispatch has started. This only completes
// when the two run concurrently; run one after another, the first group waits until the test
// gives up and sets `flags[1]`, and reports 0.
uniform RWStructuredBuffer<Atomic<uint>> flags; // [0]: the last group has started, [1]: give up
uniform RWStructuredBuffer<uint> sawLastGroup;
uniform uint lastGroup;

[shader("compute")]
[numthreads(64, 1, 1)]
void firstGroupWaitsForLast(uint3 groupID: SV_GroupID, uint3 groupThreadID: SV_GroupThreadID)
{
    if (groupThreadID.x != 0)
        return;
    if (groupID.x == lastGroup)
        flags[0].add(1);
    if (groupID.x == 0)
    {
        // add(0) is an atomic read.
        while (flags[0].add(0) == 0 && flags[1].add(0) == 0)
        {
        }
        sawLastGroup[0] = flags[0].add(0);
    }
}
