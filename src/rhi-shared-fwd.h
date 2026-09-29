#pragma once

#include "core/smart-pointer.h"

#include <cstdint>
#include <set>

namespace rhi {

class DeviceChild;
class AccelerationStructure;
class Micromap;
class Buffer;
class Resource;
class Texture;
class QueryPool;
class InputLayout;
class ShaderTable;
class Heap;

class CommandBuffer;
class CommandEncoder;
class ComputePassEncoder;
class RayTracingPassEncoder;

class CommandList;

class Device;

class ShaderObject;
class RootShaderObject;
struct ShaderObjectID;
struct ResourceSlot;
struct ExtendedShaderObjectType;
struct ExtendedShaderObjectTypeList;
class ExtendedShaderObjectTypeListObject;
class ShaderObjectLayout;

class Pipeline;
class RenderPipeline;
class VirtualRenderPipeline;
class ComputePipeline;
class VirtualComputePipeline;
class RayTracingPipeline;
class VirtualRayTracingPipeline;

struct SpecializationKey;
class ShaderProgram;
class ShaderCompilationReporter;

typedef uint32_t ShaderComponentID;

/// The set of objects a command buffer keeps alive for as long as its recorded commands may
/// still be executing on the GPU. The references are internal: an object retained only because
/// the GPU has not finished with it must not keep its device alive, or a submitted command
/// buffer would make the device immortal.
using TrackedObjectSet = std::set<InternalRefPtr<RefObject>>;

} // namespace rhi
