#pragma once

// Configuration for Vulkan Memory Allocator (VMA).
// This header is force-included when compiling vk_mem_alloc.cpp to route
// VMA assertions through slang-rhi's assertion system.

#include "core/assert.h"

#define VMA_ASSERT(expr) SLANG_RHI_ASSERT(expr)

// slang-rhi loads Vulkan dynamically and provides vkGetInstanceProcAddr
// and vkGetDeviceProcAddr via VmaVulkanFunctions. VMA uses these to
// resolve remaining function pointers at runtime.
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
