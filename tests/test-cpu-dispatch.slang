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

// Every group checks in, then waits until all groups have checked in. This only completes when
// the groups run concurrently; run one after another, the first group gives up after
// `spinBudget` polls and reports 0.
uniform RWStructuredBuffer<Atomic<uint>> arrived;
uniform RWStructuredBuffer<uint> sawAll;
uniform uint groupCount;
uniform uint spinBudget;

[shader("compute")]
[numthreads(1, 1, 1)]
void waitForAllGroups(uint3 groupID: SV_GroupID)
{
    arrived[0].add(1);
    uint budget = spinBudget;
    while (arrived[0].add(0) < groupCount && budget != 0)
        budget--;
    sawAll[groupID.x] = arrived[0].add(0) >= groupCount ? 1 : 0;
}
