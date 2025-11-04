// Host mirror for Cluster Acceleration args ABI.
// Maps ABI_U32/ABI_U64 to C++ POD types and includes the common layout.

#ifndef CLUSTER_ACCEL_ABI_HOST_H
#define CLUSTER_ACCEL_ABI_HOST_H

namespace cluster_abi {
    #define ABI_U32 uint32_t
    #define ABI_U64 uint64_t
    #include "cluster_accel_abi.common.inc"
    #undef ABI_U32
    #undef ABI_U64
}

#endif // CLUSTER_ACCEL_ABI_HOST_H


