#ifndef GALOIS_PARTITION_H
#define GALOIS_PARTITION_H

namespace galois {

struct PartitionIndexMetadata {
    uint64_t numNodes;        // 8 bytes
    uint32_t numPartitions;   // 4 bytes
    uint32_t bitmapSize;      // 4 bytes
    uint32_t sizeIndItem;     // 4 bytes
    uint32_t sizeIndIndItem;  // 4 bytes
    uint64_t numTotalParts;   // 8 bytes
};

struct PartitionedCSRMetadata {
    uint64_t numNodes;        // 8 bytes
    uint64_t numEdges;        // 8 bytes
    uint64_t numPartitions;   // 8 bytes
    uint64_t numTotalParts;   // 8 bytes
};


struct NodeMetadata {
    uint32_t edgeInd1;  // upper 2 bytes is partCnt
    uint32_t edgeInd2;
    uint32_t partInd;

    uint16_t getPartCnt() const {
        return (uint16_t)(edgeInd1 >> 16);
    }
    void setPartCnt(uint16_t val) {
        edgeInd1 &= 0x0000ffff;
        edgeInd1 |= (val << 16);
    }
    uint64_t getEdgeInd() const {
        return ((uint64_t)(edgeInd1 & 0x0000ffff) << 32) | (uint64_t)edgeInd2;
    }
    void setEdgeInd(uint64_t val) {
        edgeInd1 &= 0xffff0000;
        edgeInd1 |= (uint32_t)(val >> 32);
        edgeInd2 = (uint32_t)(val & 0x00000000ffffffff);
    }
};

struct NodePartData {
    uint16_t partId;
    uint16_t partInd1;
    uint16_t partInd2;

    uint32_t getPartInd() const {
        return *(uint32_t*)(&partInd1);
    }
    void setPartInd(uint32_t val) {
        *(uint32_t*)(&partInd1) = val;
    }
};

typedef struct PartitionIndexMetadata PartIndMetadata;
typedef struct PartitionedCSRMetadata PartitionedCSRMetadata;
typedef struct NodeMetadata           NodeMetadata;
typedef struct NodePartData           NodePartData;

} // namespace galois

#endif
