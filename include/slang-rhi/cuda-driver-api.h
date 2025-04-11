#pragma once

#define SLANG_RHI_USE_DYNAMIC_CUDA 1

extern "C" bool rhiCudaDriverApiInit();
extern "C" void rhiCudaDriverApiShutdown();

#if SLANG_RHI_USE_DYNAMIC_CUDA

#include <cstdint>
#include <cstdlib>

using cuuint32_t = uint32_t;
using cuuint64_t = uint64_t;

using CUdeviceptr = long long unsigned int;
using CUdevice = int;
using CUcontext = struct CUctx_st*;
using CUmodule = struct CUmod_st*;
using CUfunction = struct CUfunc_st*;
using CUarray = struct CUarray_st*;
using CUmipmappedArray = struct CUmipmappedArray_st*;
using CUtexref = struct CUtexref_st*;
using CUsurfref = struct CUsurfref_st*;
using CUevent = struct CUevent_st*;
using CUstream = struct CUstream_st*;
using CUgraphicsResource = struct CUgraphicsResource_st*;
using CUtexObject = uint64_t;
using CUsurfObject = uint64_t;
using CUexternalMemory = struct CUextMemory_st*;
using CUexternalSemaphore = struct CUextSemaphore_st*;
using CUfunction = struct CUfunc_st*;

#ifndef CU_UUID_HAS_BEEN_DEFINED
#define CU_UUID_HAS_BEEN_DEFINED
typedef struct CUuuid_st
{
    char bytes[16];
} CUuuid;
#endif

enum CUstream_flags
{
    CU_STREAM_DEFAULT = 0x0,
    CU_STREAM_NON_BLOCKING = 0x1,
};

enum CUevent_flags
{
    CU_EVENT_DEFAULT = 0x0,
    CU_EVENT_BLOCKING_SYNC = 0x1,
    CU_EVENT_DISABLE_TIMING = 0x2,
    CU_EVENT_INTERPROCESS = 0x4,
};

enum CUevent_wait_flags
{
    CU_EVENT_WAIT_DEFAULT = 0x0,
    CU_EVENT_WAIT_EXTERNAL = 0x1,
};

enum CUarray_format
{
    CU_AD_FORMAT_UNSIGNED_INT8 = 0x01,
    CU_AD_FORMAT_UNSIGNED_INT16 = 0x02,
    CU_AD_FORMAT_UNSIGNED_INT32 = 0x03,
    CU_AD_FORMAT_SIGNED_INT8 = 0x08,
    CU_AD_FORMAT_SIGNED_INT16 = 0x09,
    CU_AD_FORMAT_SIGNED_INT32 = 0x0a,
    CU_AD_FORMAT_HALF = 0x10,
    CU_AD_FORMAT_FLOAT = 0x20,
    CU_AD_FORMAT_NV12 = 0xb0,
    CU_AD_FORMAT_UNORM_INT8X1 = 0xc0,
    CU_AD_FORMAT_UNORM_INT8X2 = 0xc1,
    CU_AD_FORMAT_UNORM_INT8X4 = 0xc2,
    CU_AD_FORMAT_UNORM_INT16X1 = 0xc3,
    CU_AD_FORMAT_UNORM_INT16X2 = 0xc4,
    CU_AD_FORMAT_UNORM_INT16X4 = 0xc5,
    CU_AD_FORMAT_SNORM_INT8X1 = 0xc6,
    CU_AD_FORMAT_SNORM_INT8X2 = 0xc7,
    CU_AD_FORMAT_SNORM_INT8X4 = 0xc8,
    CU_AD_FORMAT_SNORM_INT16X1 = 0xc9,
    CU_AD_FORMAT_SNORM_INT16X2 = 0xca,
    CU_AD_FORMAT_SNORM_INT16X4 = 0xcb,
    CU_AD_FORMAT_BC1_UNORM = 0x91,
    CU_AD_FORMAT_BC1_UNORM_SRGB = 0x92,
    CU_AD_FORMAT_BC2_UNORM = 0x93,
    CU_AD_FORMAT_BC2_UNORM_SRGB = 0x94,
    CU_AD_FORMAT_BC3_UNORM = 0x95,
    CU_AD_FORMAT_BC3_UNORM_SRGB = 0x96,
    CU_AD_FORMAT_BC4_UNORM = 0x97,
    CU_AD_FORMAT_BC4_SNORM = 0x98,
    CU_AD_FORMAT_BC5_UNORM = 0x99,
    CU_AD_FORMAT_BC5_SNORM = 0x9a,
    CU_AD_FORMAT_BC6H_UF16 = 0x9b,
    CU_AD_FORMAT_BC6H_SF16 = 0x9c,
    CU_AD_FORMAT_BC7_UNORM = 0x9d,
    CU_AD_FORMAT_BC7_UNORM_SRGB = 0x9e,
};

enum CUaddress_mode
{
    CU_TR_ADDRESS_MODE_WRAP = 0,
    CU_TR_ADDRESS_MODE_CLAMP = 1,
    CU_TR_ADDRESS_MODE_MIRROR = 2,
    CU_TR_ADDRESS_MODE_BORDER = 3,
};

enum CUfilter_mode
{
    CU_TR_FILTER_MODE_POINT = 0,
    CU_TR_FILTER_MODE_LINEAR = 1,
};

enum CUdevice_attribute
{
    CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK = 1,
    CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X = 2,
    CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y = 3,
    CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z = 4,
    CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X = 5,
    CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y = 6,
    CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z = 7,
    CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK = 8,
    CU_DEVICE_ATTRIBUTE_SHARED_MEMORY_PER_BLOCK = 8,
    CU_DEVICE_ATTRIBUTE_TOTAL_CONSTANT_MEMORY = 9,
    CU_DEVICE_ATTRIBUTE_WARP_SIZE = 10,
    CU_DEVICE_ATTRIBUTE_MAX_PITCH = 11,
    CU_DEVICE_ATTRIBUTE_MAX_REGISTERS_PER_BLOCK = 12,
    CU_DEVICE_ATTRIBUTE_REGISTERS_PER_BLOCK = 12,
    CU_DEVICE_ATTRIBUTE_CLOCK_RATE = 13,
    CU_DEVICE_ATTRIBUTE_TEXTURE_ALIGNMENT = 14,
    CU_DEVICE_ATTRIBUTE_GPU_OVERLAP = 15,
    CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT = 16,
    CU_DEVICE_ATTRIBUTE_KERNEL_EXEC_TIMEOUT = 17,
    CU_DEVICE_ATTRIBUTE_INTEGRATED = 18,
    CU_DEVICE_ATTRIBUTE_CAN_MAP_HOST_MEMORY = 19,
    CU_DEVICE_ATTRIBUTE_COMPUTE_MODE = 20,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_WIDTH = 21,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_WIDTH = 22,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_HEIGHT = 23,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_WIDTH = 24,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_HEIGHT = 25,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_DEPTH = 26,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LAYERED_WIDTH = 27,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LAYERED_HEIGHT = 28,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LAYERED_LAYERS = 29,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_ARRAY_WIDTH = 27,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_ARRAY_HEIGHT = 28,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_ARRAY_NUMSLICES = 29,
    CU_DEVICE_ATTRIBUTE_SURFACE_ALIGNMENT = 30,
    CU_DEVICE_ATTRIBUTE_CONCURRENT_KERNELS = 31,
    CU_DEVICE_ATTRIBUTE_ECC_ENABLED = 32,
    CU_DEVICE_ATTRIBUTE_PCI_BUS_ID = 33,
    CU_DEVICE_ATTRIBUTE_PCI_DEVICE_ID = 34,
    CU_DEVICE_ATTRIBUTE_TCC_DRIVER = 35,
    CU_DEVICE_ATTRIBUTE_MEMORY_CLOCK_RATE = 36,
    CU_DEVICE_ATTRIBUTE_GLOBAL_MEMORY_BUS_WIDTH = 37,
    CU_DEVICE_ATTRIBUTE_L2_CACHE_SIZE = 38,
    CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_MULTIPROCESSOR = 39,
    CU_DEVICE_ATTRIBUTE_ASYNC_ENGINE_COUNT = 40,
    CU_DEVICE_ATTRIBUTE_UNIFIED_ADDRESSING = 41,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_LAYERED_WIDTH = 42,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_LAYERED_LAYERS = 43,
    CU_DEVICE_ATTRIBUTE_CAN_TEX2D_GATHER = 44,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_GATHER_WIDTH = 45,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_GATHER_HEIGHT = 46,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_WIDTH_ALTERNATE = 47,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_HEIGHT_ALTERNATE = 48,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_DEPTH_ALTERNATE = 49,
    CU_DEVICE_ATTRIBUTE_PCI_DOMAIN_ID = 50,
    CU_DEVICE_ATTRIBUTE_TEXTURE_PITCH_ALIGNMENT = 51,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURECUBEMAP_WIDTH = 52,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURECUBEMAP_LAYERED_WIDTH = 53,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURECUBEMAP_LAYERED_LAYERS = 54,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE1D_WIDTH = 55,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_WIDTH = 56,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_HEIGHT = 57,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE3D_WIDTH = 58,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE3D_HEIGHT = 59,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE3D_DEPTH = 60,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE1D_LAYERED_WIDTH = 61,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE1D_LAYERED_LAYERS = 62,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_LAYERED_WIDTH = 63,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_LAYERED_HEIGHT = 64,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_LAYERED_LAYERS = 65,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACECUBEMAP_WIDTH = 66,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACECUBEMAP_LAYERED_WIDTH = 67,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACECUBEMAP_LAYERED_LAYERS = 68,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_LINEAR_WIDTH = 69,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LINEAR_WIDTH = 70,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LINEAR_HEIGHT = 71,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LINEAR_PITCH = 72,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_MIPMAPPED_WIDTH = 73,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_MIPMAPPED_HEIGHT = 74,
    CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR = 75,
    CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR = 76,
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_MIPMAPPED_WIDTH = 77,
    CU_DEVICE_ATTRIBUTE_STREAM_PRIORITIES_SUPPORTED = 78,
    CU_DEVICE_ATTRIBUTE_GLOBAL_L1_CACHE_SUPPORTED = 79,
    CU_DEVICE_ATTRIBUTE_LOCAL_L1_CACHE_SUPPORTED = 80,
    CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_MULTIPROCESSOR = 81,
    CU_DEVICE_ATTRIBUTE_MAX_REGISTERS_PER_MULTIPROCESSOR = 82,
    CU_DEVICE_ATTRIBUTE_MANAGED_MEMORY = 83,
    CU_DEVICE_ATTRIBUTE_MULTI_GPU_BOARD = 84,
    CU_DEVICE_ATTRIBUTE_MULTI_GPU_BOARD_GROUP_ID = 85,
    CU_DEVICE_ATTRIBUTE_HOST_NATIVE_ATOMIC_SUPPORTED = 86,
    CU_DEVICE_ATTRIBUTE_SINGLE_TO_DOUBLE_PRECISION_PERF_RATIO = 87,
    CU_DEVICE_ATTRIBUTE_PAGEABLE_MEMORY_ACCESS = 88,
    CU_DEVICE_ATTRIBUTE_CONCURRENT_MANAGED_ACCESS = 89,
    CU_DEVICE_ATTRIBUTE_COMPUTE_PREEMPTION_SUPPORTED = 90,
    CU_DEVICE_ATTRIBUTE_CAN_USE_HOST_POINTER_FOR_REGISTERED_MEM = 91,
    CU_DEVICE_ATTRIBUTE_CAN_USE_STREAM_MEM_OPS_V1 = 92,
    CU_DEVICE_ATTRIBUTE_CAN_USE_64_BIT_STREAM_MEM_OPS_V1 = 93,
    CU_DEVICE_ATTRIBUTE_CAN_USE_STREAM_WAIT_VALUE_NOR_V1 = 94,
    CU_DEVICE_ATTRIBUTE_COOPERATIVE_LAUNCH = 95,
    CU_DEVICE_ATTRIBUTE_COOPERATIVE_MULTI_DEVICE_LAUNCH = 96,
    CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK_OPTIN = 97,
    CU_DEVICE_ATTRIBUTE_CAN_FLUSH_REMOTE_WRITES = 98,
    CU_DEVICE_ATTRIBUTE_HOST_REGISTER_SUPPORTED = 99,
    CU_DEVICE_ATTRIBUTE_PAGEABLE_MEMORY_ACCESS_USES_HOST_PAGE_TABLES = 100,
    CU_DEVICE_ATTRIBUTE_DIRECT_MANAGED_MEM_ACCESS_FROM_HOST = 101,
    CU_DEVICE_ATTRIBUTE_VIRTUAL_ADDRESS_MANAGEMENT_SUPPORTED = 102,
    CU_DEVICE_ATTRIBUTE_VIRTUAL_MEMORY_MANAGEMENT_SUPPORTED = 102,
    CU_DEVICE_ATTRIBUTE_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR_SUPPORTED = 103,
    CU_DEVICE_ATTRIBUTE_HANDLE_TYPE_WIN32_HANDLE_SUPPORTED = 104,
    CU_DEVICE_ATTRIBUTE_HANDLE_TYPE_WIN32_KMT_HANDLE_SUPPORTED = 105,
    CU_DEVICE_ATTRIBUTE_MAX_BLOCKS_PER_MULTIPROCESSOR = 106,
    CU_DEVICE_ATTRIBUTE_GENERIC_COMPRESSION_SUPPORTED = 107,
    CU_DEVICE_ATTRIBUTE_MAX_PERSISTING_L2_CACHE_SIZE = 108,
    CU_DEVICE_ATTRIBUTE_MAX_ACCESS_POLICY_WINDOW_SIZE = 109,
    CU_DEVICE_ATTRIBUTE_GPU_DIRECT_RDMA_WITH_CUDA_VMM_SUPPORTED = 110,
    CU_DEVICE_ATTRIBUTE_RESERVED_SHARED_MEMORY_PER_BLOCK = 111,
    CU_DEVICE_ATTRIBUTE_SPARSE_CUDA_ARRAY_SUPPORTED = 112,
    CU_DEVICE_ATTRIBUTE_READ_ONLY_HOST_REGISTER_SUPPORTED = 113,
    CU_DEVICE_ATTRIBUTE_TIMELINE_SEMAPHORE_INTEROP_SUPPORTED = 114,
    CU_DEVICE_ATTRIBUTE_MEMORY_POOLS_SUPPORTED = 115,
    CU_DEVICE_ATTRIBUTE_GPU_DIRECT_RDMA_SUPPORTED = 116,
    CU_DEVICE_ATTRIBUTE_GPU_DIRECT_RDMA_FLUSH_WRITES_OPTIONS = 117,
    CU_DEVICE_ATTRIBUTE_GPU_DIRECT_RDMA_WRITES_ORDERING = 118,
    CU_DEVICE_ATTRIBUTE_MEMPOOL_SUPPORTED_HANDLE_TYPES = 119,
    CU_DEVICE_ATTRIBUTE_CLUSTER_LAUNCH = 120,
    CU_DEVICE_ATTRIBUTE_DEFERRED_MAPPING_CUDA_ARRAY_SUPPORTED = 121,
    CU_DEVICE_ATTRIBUTE_CAN_USE_64_BIT_STREAM_MEM_OPS = 122,
    CU_DEVICE_ATTRIBUTE_CAN_USE_STREAM_WAIT_VALUE_NOR = 123,
    CU_DEVICE_ATTRIBUTE_DMA_BUF_SUPPORTED = 124,
    CU_DEVICE_ATTRIBUTE_IPC_EVENT_SUPPORTED = 125,
    CU_DEVICE_ATTRIBUTE_MEM_SYNC_DOMAIN_COUNT = 126,
    CU_DEVICE_ATTRIBUTE_TENSOR_MAP_ACCESS_SUPPORTED = 127,
    CU_DEVICE_ATTRIBUTE_HANDLE_TYPE_FABRIC_SUPPORTED = 128,
    CU_DEVICE_ATTRIBUTE_UNIFIED_FUNCTION_POINTERS = 129,
    CU_DEVICE_ATTRIBUTE_NUMA_CONFIG = 130,
    CU_DEVICE_ATTRIBUTE_NUMA_ID = 131,
    CU_DEVICE_ATTRIBUTE_MULTICAST_SUPPORTED = 132,
    CU_DEVICE_ATTRIBUTE_MPS_ENABLED = 133,
    CU_DEVICE_ATTRIBUTE_HOST_NUMA_ID = 134,
    CU_DEVICE_ATTRIBUTE_MAX,
};

enum CUfunction_attribute
{
    CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK = 0,
    CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES = 1,
    CU_FUNC_ATTRIBUTE_CONST_SIZE_BYTES = 2,
    CU_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES = 3,
    CU_FUNC_ATTRIBUTE_NUM_REGS = 4,
    CU_FUNC_ATTRIBUTE_PTX_VERSION = 5,
    CU_FUNC_ATTRIBUTE_BINARY_VERSION = 6,
    CU_FUNC_ATTRIBUTE_CACHE_MODE_CA = 7,
    CU_FUNC_ATTRIBUTE_MAX_DYNAMIC_SHARED_SIZE_BYTES = 8,
    CU_FUNC_ATTRIBUTE_PREFERRED_SHARED_MEMORY_CARVEOUT = 9,
    CU_FUNC_ATTRIBUTE_CLUSTER_SIZE_MUST_BE_SET = 10,
    CU_FUNC_ATTRIBUTE_REQUIRED_CLUSTER_WIDTH = 11,
    CU_FUNC_ATTRIBUTE_REQUIRED_CLUSTER_HEIGHT = 12,
    CU_FUNC_ATTRIBUTE_REQUIRED_CLUSTER_DEPTH = 13,
    CU_FUNC_ATTRIBUTE_NON_PORTABLE_CLUSTER_SIZE_ALLOWED = 14,
    CU_FUNC_ATTRIBUTE_CLUSTER_SCHEDULING_POLICY_PREFERENCE = 15,
    CU_FUNC_ATTRIBUTE_MAX,
};

enum CUmemorytype
{
    CU_MEMORYTYPE_HOST = 0x01,
    CU_MEMORYTYPE_DEVICE = 0x02,
    CU_MEMORYTYPE_ARRAY = 0x03,
    CU_MEMORYTYPE_UNIFIED = 0x04,
};

enum CUmem_advise
{
    CU_MEM_ADVISE_SET_READ_MOSTLY = 1,
    CU_MEM_ADVISE_UNSET_READ_MOSTLY = 2,
    CU_MEM_ADVISE_SET_PREFERRED_LOCATION = 3,
    CU_MEM_ADVISE_UNSET_PREFERRED_LOCATION = 4,
    CU_MEM_ADVISE_SET_ACCESSED_BY = 5,
    CU_MEM_ADVISE_UNSET_ACCESSED_BY = 6,
};

enum CUarray_cubemap_face
{
    CU_CUBEMAP_FACE_POSITIVE_X = 0x00,
    CU_CUBEMAP_FACE_NEGATIVE_X = 0x01,
    CU_CUBEMAP_FACE_POSITIVE_Y = 0x02,
    CU_CUBEMAP_FACE_NEGATIVE_Y = 0x03,
    CU_CUBEMAP_FACE_POSITIVE_Z = 0x04,
    CU_CUBEMAP_FACE_NEGATIVE_Z = 0x05,
};

enum CUresourcetype
{
    CU_RESOURCE_TYPE_ARRAY = 0x00,
    CU_RESOURCE_TYPE_MIPMAPPED_ARRAY = 0x01,
    CU_RESOURCE_TYPE_LINEAR = 0x02,
    CU_RESOURCE_TYPE_PITCH2D = 0x03,
};

enum CUresult
{
    CUDA_SUCCESS = 0,
    CUDA_ERROR_INVALID_VALUE = 1,
    CUDA_ERROR_OUT_OF_MEMORY = 2,
    CUDA_ERROR_NOT_INITIALIZED = 3,
    CUDA_ERROR_DEINITIALIZED = 4,
    CUDA_ERROR_PROFILER_DISABLED = 5,
    CUDA_ERROR_PROFILER_NOT_INITIALIZED = 6,
    CUDA_ERROR_PROFILER_ALREADY_STARTED = 7,
    CUDA_ERROR_PROFILER_ALREADY_STOPPED = 8,
    CUDA_ERROR_STUB_LIBRARY = 34,
    CUDA_ERROR_DEVICE_UNAVAILABLE = 46,
    CUDA_ERROR_NO_DEVICE = 100,
    CUDA_ERROR_INVALID_DEVICE = 101,
    CUDA_ERROR_DEVICE_NOT_LICENSED = 102,
    CUDA_ERROR_INVALID_IMAGE = 200,
    CUDA_ERROR_INVALID_CONTEXT = 201,
    CUDA_ERROR_CONTEXT_ALREADY_CURRENT = 202,
    CUDA_ERROR_MAP_FAILED = 205,
    CUDA_ERROR_UNMAP_FAILED = 206,
    CUDA_ERROR_ARRAY_IS_MAPPED = 207,
    CUDA_ERROR_ALREADY_MAPPED = 208,
    CUDA_ERROR_NO_BINARY_FOR_GPU = 209,
    CUDA_ERROR_ALREADY_ACQUIRED = 210,
    CUDA_ERROR_NOT_MAPPED = 211,
    CUDA_ERROR_NOT_MAPPED_AS_ARRAY = 212,
    CUDA_ERROR_NOT_MAPPED_AS_POINTER = 213,
    CUDA_ERROR_ECC_UNCORRECTABLE = 214,
    CUDA_ERROR_UNSUPPORTED_LIMIT = 215,
    CUDA_ERROR_CONTEXT_ALREADY_IN_USE = 216,
    CUDA_ERROR_PEER_ACCESS_UNSUPPORTED = 217,
    CUDA_ERROR_INVALID_PTX = 218,
    CUDA_ERROR_INVALID_GRAPHICS_CONTEXT = 219,
    CUDA_ERROR_NVLINK_UNCORRECTABLE = 220,
    CUDA_ERROR_JIT_COMPILER_NOT_FOUND = 221,
    CUDA_ERROR_UNSUPPORTED_PTX_VERSION = 222,
    CUDA_ERROR_JIT_COMPILATION_DISABLED = 223,
    CUDA_ERROR_UNSUPPORTED_EXEC_AFFINITY = 224,
    CUDA_ERROR_UNSUPPORTED_DEVSIDE_SYNC = 225,
    CUDA_ERROR_INVALID_SOURCE = 300,
    CUDA_ERROR_FILE_NOT_FOUND = 301,
    CUDA_ERROR_SHARED_OBJECT_SYMBOL_NOT_FOUND = 302,
    CUDA_ERROR_SHARED_OBJECT_INIT_FAILED = 303,
    CUDA_ERROR_OPERATING_SYSTEM = 304,
    CUDA_ERROR_INVALID_HANDLE = 400,
    CUDA_ERROR_ILLEGAL_STATE = 401,
    CUDA_ERROR_LOSSY_QUERY = 402,
    CUDA_ERROR_NOT_FOUND = 500,
    CUDA_ERROR_NOT_READY = 600,
    CUDA_ERROR_ILLEGAL_ADDRESS = 700,
    CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES = 701,
    CUDA_ERROR_LAUNCH_TIMEOUT = 702,
    CUDA_ERROR_LAUNCH_INCOMPATIBLE_TEXTURING = 703,
    CUDA_ERROR_PEER_ACCESS_ALREADY_ENABLED = 704,
    CUDA_ERROR_PEER_ACCESS_NOT_ENABLED = 705,
    CUDA_ERROR_PRIMARY_CONTEXT_ACTIVE = 708,
    CUDA_ERROR_CONTEXT_IS_DESTROYED = 709,
    CUDA_ERROR_ASSERT = 710,
    CUDA_ERROR_TOO_MANY_PEERS = 711,
    CUDA_ERROR_HOST_MEMORY_ALREADY_REGISTERED = 712,
    CUDA_ERROR_HOST_MEMORY_NOT_REGISTERED = 713,
    CUDA_ERROR_HARDWARE_STACK_ERROR = 714,
    CUDA_ERROR_ILLEGAL_INSTRUCTION = 715,
    CUDA_ERROR_MISALIGNED_ADDRESS = 716,
    CUDA_ERROR_INVALID_ADDRESS_SPACE = 717,
    CUDA_ERROR_INVALID_PC = 718,
    CUDA_ERROR_LAUNCH_FAILED = 719,
    CUDA_ERROR_COOPERATIVE_LAUNCH_TOO_LARGE = 720,
    CUDA_ERROR_NOT_PERMITTED = 800,
    CUDA_ERROR_NOT_SUPPORTED = 801,
    CUDA_ERROR_SYSTEM_NOT_READY = 802,
    CUDA_ERROR_SYSTEM_DRIVER_MISMATCH = 803,
    CUDA_ERROR_COMPAT_NOT_SUPPORTED_ON_DEVICE = 804,
    CUDA_ERROR_MPS_CONNECTION_FAILED = 805,
    CUDA_ERROR_MPS_RPC_FAILURE = 806,
    CUDA_ERROR_MPS_SERVER_NOT_READY = 807,
    CUDA_ERROR_MPS_MAX_CLIENTS_REACHED = 808,
    CUDA_ERROR_MPS_MAX_CONNECTIONS_REACHED = 809,
    CUDA_ERROR_MPS_CLIENT_TERMINATED = 810,
    CUDA_ERROR_CDP_NOT_SUPPORTED = 811,
    CUDA_ERROR_CDP_VERSION_MISMATCH = 812,
    CUDA_ERROR_STREAM_CAPTURE_UNSUPPORTED = 900,
    CUDA_ERROR_STREAM_CAPTURE_INVALIDATED = 901,
    CUDA_ERROR_STREAM_CAPTURE_MERGE = 902,
    CUDA_ERROR_STREAM_CAPTURE_UNMATCHED = 903,
    CUDA_ERROR_STREAM_CAPTURE_UNJOINED = 904,
    CUDA_ERROR_STREAM_CAPTURE_ISOLATION = 905,
    CUDA_ERROR_STREAM_CAPTURE_IMPLICIT = 906,
    CUDA_ERROR_CAPTURED_EVENT = 907,
    CUDA_ERROR_STREAM_CAPTURE_WRONG_THREAD = 908,
    CUDA_ERROR_TIMEOUT = 909,
    CUDA_ERROR_GRAPH_EXEC_UPDATE_FAILURE = 910,
    CUDA_ERROR_EXTERNAL_DEVICE = 911,
    CUDA_ERROR_INVALID_CLUSTER_SIZE = 912,
    CUDA_ERROR_UNKNOWN = 999,
};

struct CUDA_MEMCPY2D
{
    size_t srcXInBytes;
    size_t srcY;

    CUmemorytype srcMemoryType;
    const void* srcHost;
    CUdeviceptr srcDevice;
    CUarray srcArray;
    size_t srcPitch;

    size_t dstXInBytes;
    size_t dstY;

    CUmemorytype dstMemoryType;
    void* dstHost;
    CUdeviceptr dstDevice;
    CUarray dstArray;
    size_t dstPitch;

    size_t WidthInBytes;
    size_t Height;
};

struct CUDA_MEMCPY3D
{
    size_t srcXInBytes;
    size_t srcY;
    size_t srcZ;
    size_t srcLOD;
    CUmemorytype srcMemoryType;
    const void* srcHost;
    CUdeviceptr srcDevice;
    CUarray srcArray;
    void* reserved0;
    size_t srcPitch;
    size_t srcHeight;

    size_t dstXInBytes;
    size_t dstY;
    size_t dstZ;
    size_t dstLOD;
    CUmemorytype dstMemoryType;
    void* dstHost;
    CUdeviceptr dstDevice;
    CUarray dstArray;
    void* reserved1;
    size_t dstPitch;
    size_t dstHeight;

    size_t WidthInBytes;
    size_t Height;
    size_t Depth;
};

struct CUDA_ARRAY_DESCRIPTOR
{
    size_t Width;
    size_t Height;

    CUarray_format Format;
    unsigned int NumChannels;
};

struct CUDA_ARRAY3D_DESCRIPTOR
{
    size_t Width;
    size_t Height;
    size_t Depth;

    CUarray_format Format;
    unsigned int NumChannels;
    unsigned int Flags;
};

struct CUDA_ARRAY_MEMORY_REQUIREMENTS
{
    size_t size;
    size_t alignment;
    unsigned int reserved[4];
};

struct CUDA_RESOURCE_DESC
{
    CUresourcetype resType;

    union
    {
        struct
        {
            CUarray hArray;
        } array;
        struct
        {
            CUmipmappedArray hMipmappedArray;
        } mipmap;
        struct
        {
            CUdeviceptr devPtr;
            CUarray_format format;
            unsigned int numChannels;
            size_t sizeInBytes;
        } linear;
        struct
        {
            CUdeviceptr devPtr;
            CUarray_format format;
            unsigned int numChannels;
            size_t width;
            size_t height;
            size_t pitchInBytes;
        } pitch2D;
        struct
        {
            int reserved[32];
        } reserved;
    } res;

    unsigned int flags;
};

struct CUDA_TEXTURE_DESC
{
    CUaddress_mode addressMode[3];
    CUfilter_mode filterMode;
    unsigned int flags;
    unsigned int maxAnisotropy;
    CUfilter_mode mipmapFilterMode;
    float mipmapLevelBias;
    float minMipmapLevelClamp;
    float maxMipmapLevelClamp;
    float borderColor[4];
    int reserved[12];
};

enum CUresourceViewFormat
{
    CU_RES_VIEW_FORMAT_NONE = 0x00,
    CU_RES_VIEW_FORMAT_UINT_1X8 = 0x01,
    CU_RES_VIEW_FORMAT_UINT_2X8 = 0x02,
    CU_RES_VIEW_FORMAT_UINT_4X8 = 0x03,
    CU_RES_VIEW_FORMAT_SINT_1X8 = 0x04,
    CU_RES_VIEW_FORMAT_SINT_2X8 = 0x05,
    CU_RES_VIEW_FORMAT_SINT_4X8 = 0x06,
    CU_RES_VIEW_FORMAT_UINT_1X16 = 0x07,
    CU_RES_VIEW_FORMAT_UINT_2X16 = 0x08,
    CU_RES_VIEW_FORMAT_UINT_4X16 = 0x09,
    CU_RES_VIEW_FORMAT_SINT_1X16 = 0x0a,
    CU_RES_VIEW_FORMAT_SINT_2X16 = 0x0b,
    CU_RES_VIEW_FORMAT_SINT_4X16 = 0x0c,
    CU_RES_VIEW_FORMAT_UINT_1X32 = 0x0d,
    CU_RES_VIEW_FORMAT_UINT_2X32 = 0x0e,
    CU_RES_VIEW_FORMAT_UINT_4X32 = 0x0f,
    CU_RES_VIEW_FORMAT_SINT_1X32 = 0x10,
    CU_RES_VIEW_FORMAT_SINT_2X32 = 0x11,
    CU_RES_VIEW_FORMAT_SINT_4X32 = 0x12,
    CU_RES_VIEW_FORMAT_FLOAT_1X16 = 0x13,
    CU_RES_VIEW_FORMAT_FLOAT_2X16 = 0x14,
    CU_RES_VIEW_FORMAT_FLOAT_4X16 = 0x15,
    CU_RES_VIEW_FORMAT_FLOAT_1X32 = 0x16,
    CU_RES_VIEW_FORMAT_FLOAT_2X32 = 0x17,
    CU_RES_VIEW_FORMAT_FLOAT_4X32 = 0x18,
    CU_RES_VIEW_FORMAT_UNSIGNED_BC1 = 0x19,
    CU_RES_VIEW_FORMAT_UNSIGNED_BC2 = 0x1a,
    CU_RES_VIEW_FORMAT_UNSIGNED_BC3 = 0x1b,
    CU_RES_VIEW_FORMAT_UNSIGNED_BC4 = 0x1c,
    CU_RES_VIEW_FORMAT_SIGNED_BC4 = 0x1d,
    CU_RES_VIEW_FORMAT_UNSIGNED_BC5 = 0x1e,
    CU_RES_VIEW_FORMAT_SIGNED_BC5 = 0x1f,
    CU_RES_VIEW_FORMAT_UNSIGNED_BC6H = 0x20,
    CU_RES_VIEW_FORMAT_SIGNED_BC6H = 0x21,
    CU_RES_VIEW_FORMAT_UNSIGNED_BC7 = 0x22,
};

struct CUDA_RESOURCE_VIEW_DESC
{
    CUresourceViewFormat format;
    size_t width;
    size_t height;
    size_t depth;
    unsigned int firstMipmapLevel;
    unsigned int lastMipmapLevel;
    unsigned int firstLayer;
    unsigned int lastLayer;
    unsigned int reserved[16];
};

enum CUexternalMemoryHandleType
{
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD = 1,
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32 = 2,
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT = 3,
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP = 4,
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE = 5,
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_RESOURCE = 6,
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_RESOURCE_KMT = 7,
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_NVSCIBUF = 8,
};

#define CUDA_EXTERNAL_MEMORY_DEDICATED 0x1

struct CUDA_EXTERNAL_MEMORY_HANDLE_DESC
{
    CUexternalMemoryHandleType type;
    union
    {
        int fd;
        struct
        {
            void* handle;
            const void* name;
        } win32;
        const void* nvSciBufObject;
    } handle;
    unsigned long long size;
    unsigned int flags;
    unsigned int reserved[16];
};

struct CUDA_EXTERNAL_MEMORY_BUFFER_DESC
{
    unsigned long long offset;
    unsigned long long size;
    unsigned int flags;
    unsigned int reserved[16];
};

struct CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC
{
    unsigned long long offset;
    CUDA_ARRAY3D_DESCRIPTOR arrayDesc;
    unsigned int numLevels;
    unsigned int reserved[16];
};

enum CUexternalSemaphoreHandleType
{
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD = 1,
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32 = 2,
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_KMT = 3,
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE = 4,
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D11_FENCE = 5,
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_NVSCISYNC = 6,
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D11_KEYED_MUTEX = 7,
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D11_KEYED_MUTEX_KMT = 8,
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_FD = 9,
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_WIN32 = 10,
};

struct CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC
{
    CUexternalSemaphoreHandleType type;
    union
    {
        int fd;
        struct
        {
            void* handle;
            const void* name;
        } win32;
        const void* nvSciSyncObj;
    } handle;
    unsigned int flags;
    unsigned int reserved[16];
};

struct CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS
{
    struct
    {
        struct
        {
            unsigned long long value;
        } fence;
        union
        {
            void* fence;
            unsigned long long reserved;
        } nvSciSync;
        struct
        {
            unsigned long long key;
        } keyedMutex;
        unsigned int reserved[12];
    } params;
    unsigned int flags;
    unsigned int reserved[16];
};

struct CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS
{
    struct
    {
        struct
        {
            unsigned long long value;
        } fence;
        union
        {
            void* fence;
            unsigned long long reserved;
        } nvSciSync;
        struct
        {
            unsigned long long key;
            unsigned int timeoutMs;
        } keyedMutex;
        unsigned int reserved[10];
    } params;
    unsigned int flags;
    unsigned int reserved[16];
};

enum CUmemLocationType
{
    CU_MEM_LOCATION_TYPE_INVALID = 0x0,
    CU_MEM_LOCATION_TYPE_DEVICE = 0x1,
    CU_MEM_LOCATION_TYPE_HOST = 0x2,
    CU_MEM_LOCATION_TYPE_HOST_NUMA = 0x3,
    CU_MEM_LOCATION_TYPE_HOST_NUMA_CURRENT = 0x4,
    CU_MEM_LOCATION_TYPE_MAX = 0x7FFFFFFF,
};

struct CUmemLocation
{
    CUmemLocationType type;
    int id;
};

#define CUDA_ARRAY3D_LAYERED 0x01
#define CUDA_ARRAY3D_SURFACE_LDST 0x02
#define CUDA_ARRAY3D_CUBEMAP 0x04
#define CUDA_ARRAY3D_TEXTURE_GATHER 0x08
#define CUDA_ARRAY3D_DEPTH_TEXTURE 0x10
#define CUDA_ARRAY3D_COLOR_ATTACHMENT 0x20
#define CUDA_ARRAY3D_SPARSE 0x40
#define CUDA_ARRAY3D_DEFERRED_MAPPING 0x80

#define CU_DEVICE_CPU ((CUdevice) - 1)
#define CU_DEVICE_INVALID ((CUdevice) - 2)

#if !defined(CUDA_SYM)
#define CUDA_SYM(x) extern x;
#endif

// clang-format off
CUDA_SYM(CUresult (*cuGetErrorString)(CUresult, const char**));
CUDA_SYM(CUresult (*cuGetErrorName)(CUresult, const char**));
CUDA_SYM(CUresult (*cuInit)(unsigned int));
CUDA_SYM(CUresult (*cuDriverGetVersion)(int*));
CUDA_SYM(CUresult (*cuDeviceGet)(CUdevice*, int));
CUDA_SYM(CUresult (*cuDeviceGetCount)(int*));
CUDA_SYM(CUresult (*cuDeviceGetName)(char*, int, CUdevice));
CUDA_SYM(CUresult (*cuDeviceGetUuid)(CUuuid*, CUdevice)); // v2
CUDA_SYM(CUresult (*cuDeviceGetLuid)(char*, unsigned int*, CUdevice));
CUDA_SYM(CUresult (*cuDeviceTotalMem)(size_t*, CUdevice)); // v2
CUDA_SYM(CUresult (*cuDeviceGetAttribute)(int*, CUdevice_attribute, CUdevice));
CUDA_SYM(CUresult (*cuDevicePrimaryCtxRetain)(CUcontext*, CUdevice));
CUDA_SYM(CUresult (*cuDevicePrimaryCtxRelease)(CUdevice)); // v2
CUDA_SYM(CUresult (*cuDevicePrimaryCtxReset)(CUdevice)); // v2
CUDA_SYM(CUresult (*cuCtxCreate)(CUcontext*, unsigned int, CUdevice)); // v2
CUDA_SYM(CUresult (*cuCtxDestroy)(CUcontext)); // v2
CUDA_SYM(CUresult (*cuCtxPushCurrent)(CUcontext)); // v2
CUDA_SYM(CUresult (*cuCtxPopCurrent)(CUcontext*)); // v2
CUDA_SYM(CUresult (*cuCtxSetCurrent)(CUcontext));
CUDA_SYM(CUresult (*cuCtxGetCurrent)(CUcontext*));
CUDA_SYM(CUresult (*cuCtxGetDevice)(CUdevice*));
CUDA_SYM(CUresult (*cuCtxSynchronize)());
CUDA_SYM(CUresult (*cuMemGetInfo)(size_t*, size_t*)); // v2
CUDA_SYM(CUresult (*cuMemAlloc)(CUdeviceptr*, size_t)); // v2
CUDA_SYM(CUresult (*cuMemFree)(CUdeviceptr)); // v2
CUDA_SYM(CUresult (*cuMemAllocHost)(void**, size_t)); //v2
CUDA_SYM(CUresult (*cuMemFreeHost)(void*));
CUDA_SYM(CUresult (*cuMemAllocManaged)(CUdeviceptr*, size_t, unsigned int));
CUDA_SYM(CUresult (*cuMemcpy)(CUdeviceptr, CUdeviceptr, size_t));
CUDA_SYM(CUresult (*cuMemcpyHtoD)(CUdeviceptr, const void*, size_t)); // v2
CUDA_SYM(CUresult (*cuMemcpyDtoH)(void*, CUdeviceptr, size_t)); // v2
CUDA_SYM(CUresult (*cuMemcpyDtoD)(CUdeviceptr, CUdeviceptr, size_t)); // v2
CUDA_SYM(CUresult (*cuMemcpy2D)(const CUDA_MEMCPY2D*)); // v2
CUDA_SYM(CUresult (*cuMemcpy2DUnaligned)(const CUDA_MEMCPY2D*)); // v2
CUDA_SYM(CUresult (*cuMemcpy3D)(const CUDA_MEMCPY3D*)); // v2
CUDA_SYM(CUresult (*cuMemcpyAsync)(CUdeviceptr, CUdeviceptr, size_t, CUstream));
CUDA_SYM(CUresult (*cuMemcpyHtoDAsync)(CUdeviceptr, const void*, size_t, CUstream)); // v2
CUDA_SYM(CUresult (*cuMemcpyDtoHAsync)(void*, CUdeviceptr, size_t, CUstream)); // v2
CUDA_SYM(CUresult (*cuMemcpyDtoDAsync)(CUdeviceptr, CUdeviceptr, size_t, CUstream)); // v2
CUDA_SYM(CUresult (*cuMemcpy2DAsync)(const CUDA_MEMCPY2D*, CUstream)); // v2
CUDA_SYM(CUresult (*cuMemcpy3DAsync)(const CUDA_MEMCPY3D*, CUstream)); // v2
CUDA_SYM(CUresult (*cuMemsetD8)(CUdeviceptr, unsigned char, size_t)); // v2
CUDA_SYM(CUresult (*cuMemsetD16)(CUdeviceptr, unsigned short, size_t)); // v2
CUDA_SYM(CUresult (*cuMemsetD32)(CUdeviceptr, unsigned int, size_t)); // v2
CUDA_SYM(CUresult (*cuMemsetD2D8)(CUdeviceptr, size_t, unsigned char, size_t, size_t)); // v2
CUDA_SYM(CUresult (*cuMemsetD2D16)(CUdeviceptr, size_t, unsigned short, size_t, size_t)); // v2
CUDA_SYM(CUresult (*cuMemsetD2D32)(CUdeviceptr, size_t, unsigned int, size_t, size_t)); // v2
CUDA_SYM(CUresult (*cuMemsetD8Async)(CUdeviceptr, unsigned char, size_t, CUstream));
CUDA_SYM(CUresult (*cuMemsetD16Async)(CUdeviceptr, unsigned short, size_t, CUstream));
CUDA_SYM(CUresult (*cuMemsetD32Async)(CUdeviceptr, unsigned int, size_t, CUstream));
CUDA_SYM(CUresult (*cuMemsetD2D8Async)(CUdeviceptr, size_t, unsigned char, size_t, size_t, CUstream));
CUDA_SYM(CUresult (*cuMemsetD2D16Async)(CUdeviceptr, size_t, unsigned short, size_t, size_t, CUstream));
CUDA_SYM(CUresult (*cuMemsetD2D32Async)(CUdeviceptr, size_t, unsigned int, size_t, size_t, CUstream));
CUDA_SYM(CUresult (*cuMemAdvise)(CUdeviceptr, size_t, CUmem_advise, CUmemLocation)); // v2
CUDA_SYM(CUresult (*cuStreamCreate)(CUstream*, unsigned int));
CUDA_SYM(CUresult (*cuStreamCreateWithPriority)(CUstream*, unsigned int, int));
CUDA_SYM(CUresult (*cuStreamWaitEvent)(CUstream, CUevent, unsigned int));
CUDA_SYM(CUresult (*cuStreamSynchronize)(CUstream));
CUDA_SYM(CUresult (*cuStreamDestroy)(CUstream)); // v2
CUDA_SYM(CUresult (*cuEventCreate)(CUevent*, unsigned int));
CUDA_SYM(CUresult (*cuEventRecord)(CUevent, CUstream));
CUDA_SYM(CUresult (*cuEventQuery)(CUevent));
CUDA_SYM(CUresult (*cuEventSynchronize)(CUevent));
CUDA_SYM(CUresult (*cuEventDestroy)(CUevent)); // v2
CUDA_SYM(CUresult (*cuEventElapsedTime)(float*, CUevent, CUevent));
CUDA_SYM(CUresult (*cuImportExternalMemory)(CUexternalMemory*, const CUDA_EXTERNAL_MEMORY_HANDLE_DESC*));
CUDA_SYM(CUresult (*cuExternalMemoryGetMappedBuffer)(CUdeviceptr*, CUexternalMemory, const CUDA_EXTERNAL_MEMORY_BUFFER_DESC*));
CUDA_SYM(CUresult (*cuExternalMemoryGetMappedMipmappedArray)(CUmipmappedArray*, CUexternalMemory, const CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC*));
CUDA_SYM(CUresult (*cuDestroyExternalMemory)(CUexternalMemory));
CUDA_SYM(CUresult (*cuImportExternalSemaphore)(CUexternalSemaphore*, const CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC*));
CUDA_SYM(CUresult (*cuSignalExternalSemaphoresAsync)(const CUexternalSemaphore*, const CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS*, unsigned int, CUstream));
CUDA_SYM(CUresult (*cuWaitExternalSemaphoresAsync)(const CUexternalSemaphore*, const CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS*, unsigned int, CUstream));
CUDA_SYM(CUresult (*cuDestroyExternalSemaphore)(CUexternalSemaphore));
CUDA_SYM(CUresult (*cuModuleGetFunction)(CUfunction*, CUmodule, const char*));
CUDA_SYM(CUresult (*cuModuleGetGlobal)(CUdeviceptr*, size_t*, CUmodule, const char*)); // v2
CUDA_SYM(CUresult (*cuModuleGetTexRef)(CUtexref*, CUmodule, const char*));
CUDA_SYM(CUresult (*cuModuleLoad)(CUmodule*, const char*));
CUDA_SYM(CUresult (*cuModuleLoadData)(CUmodule*, const void*));
CUDA_SYM(CUresult (*cuModuleUnload)(CUmodule));
CUDA_SYM(CUresult (*cuFuncGetAttribute)(int*, CUfunction_attribute, CUfunction));
CUDA_SYM(CUresult (*cuFuncGetParamInfo)(CUfunction, size_t, size_t*, size_t*));
CUDA_SYM(CUresult (*cuLaunchKernel)(CUfunction, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, CUstream, void **, void **));
CUDA_SYM(CUresult (*cuMipmappedArrayGetLevel)(CUarray*, CUmipmappedArray, unsigned int));
CUDA_SYM(CUresult (*cuArrayCreate)(CUarray*, const CUDA_ARRAY_DESCRIPTOR*)); // v2
CUDA_SYM(CUresult (*cuArrayDestroy)(CUarray));
CUDA_SYM(CUresult (*cuArrayGetDescriptor)(CUDA_ARRAY_DESCRIPTOR*, CUarray)); // v2
CUDA_SYM(CUresult (*cuMipmappedArrayCreate)(CUmipmappedArray*, const CUDA_ARRAY3D_DESCRIPTOR*, unsigned int));
CUDA_SYM(CUresult (*cuMipmappedArrayDestroy)(CUmipmappedArray));
CUDA_SYM(CUresult (*cuArray3DCreate)(CUarray*, const CUDA_ARRAY3D_DESCRIPTOR*)); // v2
CUDA_SYM(CUresult (*cuSurfObjectCreate)(CUsurfObject*, const CUDA_RESOURCE_DESC*));
CUDA_SYM(CUresult (*cuSurfObjectDestroy)(CUsurfObject));
CUDA_SYM(CUresult (*cuTexObjectCreate)(CUtexObject*, const CUDA_RESOURCE_DESC*, const CUDA_TEXTURE_DESC*, const CUDA_RESOURCE_VIEW_DESC*));
CUDA_SYM(CUresult (*cuTexObjectDestroy)(CUtexObject));
// clang-format on

#define CU_LAUNCH_PARAM_END_AS_INT 0x00
#define CU_LAUNCH_PARAM_END ((void*)CU_LAUNCH_PARAM_END_AS_INT)
#define CU_LAUNCH_PARAM_BUFFER_POINTER_AS_INT 0x01
#define CU_LAUNCH_PARAM_BUFFER_POINTER ((void*)CU_LAUNCH_PARAM_BUFFER_POINTER_AS_INT)
#define CU_LAUNCH_PARAM_BUFFER_SIZE_AS_INT 0x02
#define CU_LAUNCH_PARAM_BUFFER_SIZE ((void*)CU_LAUNCH_PARAM_BUFFER_SIZE_AS_INT)

enum CUcomputemode
{
    CU_COMPUTEMODE_DEFAULT = 0,
    CU_COMPUTEMODE_PROHIBITED = 2,
    CU_COMPUTEMODE_EXCLUSIVE_PROCESS = 3,
};

#define CU_TRSF_READ_AS_INTEGER 0x01
#define CU_TRSF_NORMALIZED_COORDINATES 0x02
#define CU_TRSF_SRGB 0x10
#define CU_TRSF_DISABLE_TRILINEAR_OPTIMIZATION 0x20
#define CU_TRSF_SEAMLESS_CUBEMAP 0x40

enum CUmemAttach_flags
{
    CU_MEM_ATTACH_GLOBAL = 0x1,
    CU_MEM_ATTACH_HOST = 0x2,
    CU_MEM_ATTACH_SINGLE = 0x4,
};

#else // SLANG_RHI_USE_DYNAMIC_CUDA

#include <cuda.h>

#endif // SLANG_RHI_USE_DYNAMIC_CUDA
