// Device-only ABI for Cluster Acceleration args.
// Thin wrapper that maps ABI types for Slang and includes the shared common.

#ifndef CLUSTER_ACCEL_ABI_SLANG
#define CLUSTER_ACCEL_ABI_SLANG

namespace cluster_abi {
    #define ABI_U32 uint
    #define ABI_U64 uint64_t
    #include <cluster_accel/cluster_accel_abi.common.inc>
    #undef ABI_U32
    #undef ABI_U64
}

#endif // CLUSTER_ACCEL_ABI_SLANG


