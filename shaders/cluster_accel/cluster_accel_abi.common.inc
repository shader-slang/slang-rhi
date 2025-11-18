// Shared ABI for Cluster Acceleration args.
// This header is included from Slang with ABI_U32/ABI_U64 mapped to device types.

// -------------------------------------------------------------------------------------------------
// Constants
// -------------------------------------------------------------------------------------------------
static const ABI_U32 CLUSTER_HANDLE_BYTE_STRIDE = 8;
static const ABI_U32 CLUSTER_OUTPUT_ALIGNMENT   = 128;

// Packed fields layout for TrianglesArgs::packed0 (bit positions)
// bits [8:0]   = triangleCount
// bits [23:9]  = vertexCount
// bits [29:24] = indexFormat   (0=u16, 1=u32; others reserved)
// bits [33:30] = ommIndexFormat (MVP: must be 0)
static const ABI_U32 TRIANGLE_COUNT_SHIFT              = 0;
static const ABI_U32 TRIANGLE_COUNT_WIDTH              = 9;
static const ABI_U32 TRIANGLE_COUNT_MASK               = ((1u << TRIANGLE_COUNT_WIDTH) - 1u);
static const ABI_U32 VERTEX_COUNT_SHIFT                = 9;
static const ABI_U32 VERTEX_COUNT_WIDTH                = 9;
static const ABI_U32 VERTEX_COUNT_MASK                 = ((1u << VERTEX_COUNT_WIDTH) - 1u);
static const ABI_U32 POSITION_TRUNCATE_BITS_SHIFT      = 18;
static const ABI_U32 POSITION_TRUNCATE_BITS_WIDTH      = 6;
static const ABI_U32 POSITION_TRUNCATE_BITS_MASK       = ((1u << POSITION_TRUNCATE_BITS_WIDTH) - 1u);
static const ABI_U32 INDEX_FORMAT_SHIFT                = 24;
static const ABI_U32 INDEX_FORMAT_WIDTH                = 4;
static const ABI_U32 INDEX_FORMAT_MASK                 = ((1u << INDEX_FORMAT_WIDTH) - 1u);
static const ABI_U32 OMM_INDEX_FORMAT_SHIFT            = 28;
static const ABI_U32 OMM_INDEX_FORMAT_WIDTH            = 4;
static const ABI_U32 OMM_INDEX_FORMAT_MASK             = ((1u << OMM_INDEX_FORMAT_WIDTH) - 1u);

// PrimitiveInfo bit packing (matches 24-bit sbtIndex + 3-bit primitiveFlags)
static const ABI_U32 PRIM_SBT_INDEX_SHIFT              = 0;
static const ABI_U32 PRIM_SBT_INDEX_WIDTH              = 24;
static const ABI_U32 PRIM_SBT_INDEX_MASK               = ((1u << PRIM_SBT_INDEX_WIDTH) - 1u);
static const ABI_U32 PRIM_RESERVED_WIDTH               = 5;
static const ABI_U32 PRIM_FLAGS_SHIFT                  = PRIM_SBT_INDEX_SHIFT + PRIM_SBT_INDEX_WIDTH + PRIM_RESERVED_WIDTH;
static const ABI_U32 PRIM_FLAGS_WIDTH                  = 3;
static const ABI_U32 PRIM_FLAGS_MASK                   = ((1u << PRIM_FLAGS_WIDTH) - 1u);

// Helper to pack counts and formats into a single u32.
static inline ABI_U32 packCountsAndFormats(
    ABI_U32 triangleCount,
    ABI_U32 vertexCount,
    ABI_U32 positionTruncateBitCount,
    ABI_U32 indexFormat,
    ABI_U32 opacityMicromapIndexFormat)
{
    ABI_U32 v = 0;
    v |= (triangleCount              & TRIANGLE_COUNT_MASK)         << TRIANGLE_COUNT_SHIFT;
    v |= (vertexCount                & VERTEX_COUNT_MASK)           << VERTEX_COUNT_SHIFT;
    v |= (positionTruncateBitCount   & POSITION_TRUNCATE_BITS_MASK) << POSITION_TRUNCATE_BITS_SHIFT;
    v |= (indexFormat                & INDEX_FORMAT_MASK)           << INDEX_FORMAT_SHIFT;
    v |= (opacityMicromapIndexFormat & OMM_INDEX_FORMAT_MASK)       << OMM_INDEX_FORMAT_SHIFT;
    return v;
}

// Helper to pack primitive info word.
static inline ABI_U32 packPrimitiveInfo(ABI_U32 sbtIndex24, ABI_U32 primitiveFlags3)
{
    ABI_U32 v = 0;
    v |= (sbtIndex24      & PRIM_SBT_INDEX_MASK) << PRIM_SBT_INDEX_SHIFT;
    // 5-bit reserved region is kept zero
    v |= (primitiveFlags3 & PRIM_FLAGS_MASK)     << PRIM_FLAGS_SHIFT;
    return v;
}

// Pack two u16 strides into one u32 (low 16 / high 16)
static inline ABI_U32 packStrides16(ABI_U32 low16, ABI_U32 high16)
{
    return (low16 & 0xFFFFu) | ((high16 & 0xFFFFu) << 16);
}

// -------------------------------------------------------------------------------------------------
// Args structures (device layout)
// -------------------------------------------------------------------------------------------------
struct TrianglesArgs
{
    ABI_U32  clusterId;
    ABI_U32  clusterFlags;

    // Packed fields
    ABI_U32  packed0;        // counts + formats
    ABI_U32  primitiveInfo;  // sbtIndex + primitiveFlags (reserved bits zero)

    // Two u32 of four u16 strides: (index, vertex) and (primitiveInfo, ommIndex)
    ABI_U32  strides01; // low16 = indexBufferStrideInBytes, high16 = vertexBufferStrideInBytes
    ABI_U32  strides23; // low16 = primitiveInfoBufferStrideInBytes, high16 = opacityMicromapIndexBufferStrideInBytes

    // Device addresses (64-bit)
    ABI_U64  indexBuffer;
    ABI_U64  vertexBuffer;
    ABI_U64  primitiveInfoBuffer;
    ABI_U64  opacityMicromapArray;
    ABI_U64  opacityMicromapIndexBuffer;
    ABI_U64  instantiationBoundingBoxLimit;
};

// Args for CLAS instantiation from Templates.
// Matches OptiX OptixClusterAccelBuildInputTemplatesArgs layout.
struct TemplatesArgs
{
    ABI_U32  clusterIdOffset;     // added to template's baseClusterId
    ABI_U32  sbtIndexOffset;      // added to template's base SBT index
    ABI_U64  clusterTemplate;     // opaque template handle/device address
    ABI_U64  vertexBuffer;        // optional in GET_SIZES (can be 0 to query worst-case size)
    ABI_U32  vertexStrideInBytes;
    ABI_U32  reserved;
};

struct ClustersArgs
{
    ABI_U32  clusterHandlesCount;
    ABI_U32  clusterHandlesBufferStrideInBytes; // typically 8
    ABI_U64  clusterHandlesBuffer;              // base address of handle table
};

// Convenience initializers
static inline TrianglesArgs makeTrianglesArgs(
    ABI_U32  clusterId,
    ABI_U32  triangleCount,
    ABI_U32  vertexCount,
    ABI_U64  indexBuffer,
    ABI_U64  vertexBuffer,
    ABI_U32  vertexStrideBytes,
    ABI_U32  indexFormat)
{
    TrianglesArgs a = {};
    a.clusterId    = clusterId;
    a.clusterFlags = 0;
    a.packed0      = packCountsAndFormats(triangleCount, vertexCount, /*truncate*/0, indexFormat, /*ommIdxFmt*/0);
    a.primitiveInfo = packPrimitiveInfo(/*sbtIndex*/0, /*primFlags*/0);
    a.strides01    = packStrides16(/*index*/0, /*vertex*/vertexStrideBytes);
    a.strides23    = packStrides16(/*primInfo*/0, /*ommIdx*/0);
    a.indexBuffer  = indexBuffer;
    a.vertexBuffer = vertexBuffer;
    a.primitiveInfoBuffer = 0;
    a.opacityMicromapArray = 0;
    a.opacityMicromapIndexBuffer = 0;
    a.instantiationBoundingBoxLimit = 0;
    return a;
}

static inline TemplatesArgs makeTemplatesArgs(
    ABI_U64 clusterTemplate,
    ABI_U64 vertexBuffer,
    ABI_U32 vertexStrideInBytes,
    ABI_U32 clusterIdOffset,
    ABI_U32 sbtIndexOffset)
{
    TemplatesArgs a = {};
    a.clusterIdOffset = clusterIdOffset;
    a.sbtIndexOffset = sbtIndexOffset;
    a.clusterTemplate = clusterTemplate;
    a.vertexBuffer = vertexBuffer;                 // may be 0 in GET_SIZES mode
    a.vertexStrideInBytes = vertexStrideInBytes;   // ignored if vertexBuffer is 0
    a.reserved = 0;
    return a;
}

static inline ClustersArgs makeClustersArgs(ABI_U32 count, ABI_U64 handlesBuffer, ABI_U32 strideBytes)
{
    ClustersArgs a = {};
    a.clusterHandlesCount = count;
    a.clusterHandlesBufferStrideInBytes = strideBytes;
    a.clusterHandlesBuffer = handlesBuffer;
    return a;
}
