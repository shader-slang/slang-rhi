// Based on https://github.com/sebbbi/OffsetAllocator

// (C) Sebastian Aaltonen 2023
// MIT License

// #define USE_16_BIT_NODE_INDICES

#include <cstdint>

namespace rhi {

class OffsetAllocator
{
public:
// 16 bit offsets mode will halve the metadata storage cost
// But it only supports up to 65536 maximum allocation count
#ifdef USE_16_BIT_NODE_INDICES
    typedef uint16_t NodeIndex;
#else
    typedef uint32_t NodeIndex;
#endif

    static constexpr uint32_t NUM_TOP_BINS = 32;
    static constexpr uint32_t BINS_PER_LEAF = 8;
    static constexpr uint32_t TOP_BINS_INDEX_SHIFT = 3;
    static constexpr uint32_t LEAF_BINS_INDEX_MASK = 0x7;
    static constexpr uint32_t NUM_LEAF_BINS = NUM_TOP_BINS * BINS_PER_LEAF;

    struct Allocation
    {
        static constexpr uint32_t NO_SPACE = 0xffffffff;

        uint32_t offset = NO_SPACE;
        NodeIndex metadata = NO_SPACE; // internal: node index

        bool isValid() const { return offset != NO_SPACE; }
        explicit operator bool() const { return isValid(); }
    };

    struct StorageReport
    {
        uint32_t totalFreeSpace;
        uint32_t largestFreeRegion;
    };

    struct StorageReportFull
    {
        struct Region
        {
            uint32_t size;
            uint32_t count;
        };
        Region freeRegions[NUM_LEAF_BINS];
    };

    OffsetAllocator(uint32_t size, uint32_t maxAllocs = 128 * 1024);
    OffsetAllocator(OffsetAllocator&& other);
    ~OffsetAllocator();
    void reset();

    Allocation allocate(uint32_t size);
    void free(Allocation allocation);

    uint32_t allocationSize(Allocation allocation) const;
    StorageReport storageReport() const;
    StorageReportFull storageReportFull() const;

    uint32_t getSize() const { return m_size; }
    uint32_t getMaxAllocs() const { return m_maxAllocs; }
    uint32_t getFreeStorage() const { return m_freeStorage; }
    uint32_t getCurrentAllocs() const { return m_currentAllocs; }

private:
    uint32_t insertNodeIntoBin(uint32_t size, uint32_t dataOffset);
    void removeNodeFromBin(uint32_t nodeIndex);

    struct Node
    {
        static constexpr NodeIndex UNUSED = 0xffffffff;

        uint32_t dataOffset = 0;
        uint32_t dataSize = 0;
        NodeIndex binListPrev = UNUSED;
        NodeIndex binListNext = UNUSED;
        NodeIndex neighborPrev = UNUSED;
        NodeIndex neighborNext = UNUSED;
        bool used = false; // TODO: Merge as bit flag
    };

    uint32_t m_size;
    uint32_t m_maxAllocs;
    uint32_t m_freeStorage;
    uint32_t m_currentAllocs;

    uint32_t m_usedBinsTop;
    uint8_t m_usedBins[NUM_TOP_BINS];
    NodeIndex m_binIndices[NUM_LEAF_BINS];

    Node* m_nodes;
    NodeIndex* m_freeNodes;
    uint32_t m_freeOffset;
};

} // namespace rhi
