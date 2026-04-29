#pragma once

// Configuration for D3D12 Memory Allocator (D3D12MA).
// This header is force-included when compiling D3D12MemAlloc.cpp to route
// D3D12MA assertions through slang-rhi's assertion system.

#include "core/assert.h"

#define D3D12MA_ASSERT(cond) SLANG_RHI_ASSERT(cond)
