// test-cooperative-launch.slang - Grid-wide barrier, legal only with a cooperative launch.

// Slang has no grid-wide barrier intrinsic; use the CUDA cooperative groups API directly.
void gridSync()
{
    __target_switch
    {
    case cuda:
        __requirePrelude("#include <cooperative_groups.h>");
        __intrinsic_asm "cooperative_groups::this_grid().sync()";
    }
}

uniform RWStructuredBuffer<uint> buffer;
uniform uint groupCount;

// Each group writes its index + 1, then (after the grid barrier) thread 0 sums all of them.
// Without co-residency of every group, the barrier deadlocks or the sum misses groups.
[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 tid: SV_DispatchThreadID, uint3 gid: SV_GroupID, uint3 gtid: SV_GroupThreadID)
{
    if (gtid.x == 0)
        buffer[gid.x] = gid.x + 1;
    gridSync();
    if (tid.x == 0)
    {
        uint sum = 0;
        for (uint i = 0; i < groupCount; i++)
            sum += buffer[i];
        buffer[groupCount] = sum;
    }
}
