#pragma once

#include <slang-rhi.h>

namespace rhi {

inline size_t getCooperativeVectorComponentSize(CooperativeVectorComponentType type)
{
    switch (type)
    {
    case CooperativeVectorComponentType::Sint8:
    case CooperativeVectorComponentType::Uint8:
    case CooperativeVectorComponentType::Sint8Packed:
    case CooperativeVectorComponentType::Uint8Packed:
    case CooperativeVectorComponentType::FloatE4M3:
    case CooperativeVectorComponentType::FloatE5M2:
        return 1;
    case CooperativeVectorComponentType::Float16:
    case CooperativeVectorComponentType::Sint16:
    case CooperativeVectorComponentType::Uint16:
        return 2;
    case CooperativeVectorComponentType::Float32:
    case CooperativeVectorComponentType::Sint32:
    case CooperativeVectorComponentType::Uint32:
        return 4;
    case CooperativeVectorComponentType::Float64:
    case CooperativeVectorComponentType::Sint64:
    case CooperativeVectorComponentType::Uint64:
        return 8;
    }
    return 0;
}

inline size_t getTightRowColumnStride(
    uint32_t rowCount,
    uint32_t colCount,
    CooperativeVectorComponentType componentType,
    CooperativeVectorMatrixLayout layout
)
{
    size_t componentSize = getCooperativeVectorComponentSize(componentType);
    switch (layout)
    {
    case CooperativeVectorMatrixLayout::RowMajor:
        return componentSize * colCount;
    case CooperativeVectorMatrixLayout::ColumnMajor:
        return componentSize * rowCount;
    case CooperativeVectorMatrixLayout::InferencingOptimal:
    case CooperativeVectorMatrixLayout::TrainingOptimal:
        break;
    }
    return 0;
}

/// Compute the matrix size in bytes for non-optimal (RowMajor/ColumnMajor) layouts.
/// For optimal layouts, this returns 0 and the backend-specific API should be used.
inline size_t computeCooperativeVectorMatrixSize(
    uint32_t rowCount,
    uint32_t colCount,
    CooperativeVectorComponentType componentType,
    CooperativeVectorMatrixLayout layout,
    size_t rowColumnStride = 0
)
{
    size_t tightStride = getTightRowColumnStride(rowCount, colCount, componentType, layout);
    size_t stride = rowColumnStride != 0 ? rowColumnStride : tightStride;

    // The last row/column uses tight packing, not the padded stride.
    // Total size = (count - 1) * stride + tightStride
    switch (layout)
    {
    case CooperativeVectorMatrixLayout::RowMajor:
        return (rowCount - 1) * stride + tightStride;
    case CooperativeVectorMatrixLayout::ColumnMajor:
        return (colCount - 1) * stride + tightStride;
    case CooperativeVectorMatrixLayout::InferencingOptimal:
    case CooperativeVectorMatrixLayout::TrainingOptimal:
        // Optimal layouts are implementation-defined, return 0 to indicate
        // that the backend-specific API should be used.
        break;
    }
    return 0;
}

} // namespace rhi
