#pragma once

#include <slang-rhi.h>

#include <string>

namespace rhi {

const char* enumToString(DeviceType value);
const char* enumToString(Format value);
const char* enumToString(FormatSupport value);
const char* enumToString(MemoryType value);
const char* enumToString(BufferUsage value);
std::string flagsToString(BufferUsage value);
const char* enumToString(TextureType value);
const char* enumToString(TextureAspect value);
const char* enumToString(TextureUsage value);
std::string flagsToString(TextureUsage value);
const char* enumToString(ResourceState value);
const char* enumToString(TextureFilteringMode value);
const char* enumToString(TextureAddressingMode value);
const char* enumToString(ComparisonFunc value);
const char* enumToString(TextureReductionOp value);
const char* enumToString(InputSlotClass value);
const char* enumToString(PrimitiveTopology value);
const char* enumToString(QueryType value);
const char* enumToString(CooperativeVectorComponentType value);

} // namespace rhi
