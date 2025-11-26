#include "example-base.h"

#include <slang-rhi/acceleration-structure-utils.h>

#include <linalg.h>

#include <map>
#include <random>

using namespace rhi;
using namespace linalg::aliases;

#define USE_RAYTRACING_PIPELINE 1

struct RNG {
    std::mt19937 generator;

    RNG(uint32_t seed = 123456789)
        : generator(seed)
    {
    }

    uint32_t nextU32()
    {
        return generator();
    }

    float nextFloat()
    {
        return float(nextU32()) * (1.f / float(0xFFFFFFFF));
    }

    float3 nextFloat3()
    {
        return float3(nextFloat(), nextFloat(), nextFloat());
    }
};

struct Camera
{
    uint32_t width{100};
    uint32_t height{100};
    float aspectRatio{1.0f};
    float3 position{1, 1, 1};
    float3 target{0, 0, 0};
    float3 fwd;
    float3 right;
    float3 up{0, 1, 0};
    float fov{70.0f};

    float3 imageU;
    float3 imageV;
    float3 imageW;

    Camera() { recompute(); }

    void recompute()
    {
        aspectRatio = float(width) / float(height);

        fwd = normalize(target - position);
        right = normalize(cross(fwd, up));
        up = normalize(cross(right, fwd));

        float fovRad = radians(fov);

        imageU = right * std::tan(fovRad * 0.5f) * aspectRatio;
        imageV = up * std::tan(fovRad * 0.5f);
        imageW = fwd;
    }

    void bind(rhi::ShaderCursor cursor) const
    {
        cursor["position"].setData(position);
        cursor["imageU"].setData(imageU);
        cursor["imageV"].setData(imageV);
        cursor["imageW"].setData(imageW);
    }
};


struct CameraController
{
    Camera* camera;
    bool mouseDown{false};
    float2 mousePos;
    std::map<int, bool> keyState;
    bool shiftDown{false};
    float3 moveDelta{0.f};
    float2 rotateDelta{0.f};
    float moveSpeed{1.0f};
    float rotateSpeed{0.002f};

    static constexpr float kMoveShiftFactor = 10.0f;
    static inline const std::map<int, float3> kMoveDirection = {
        {GLFW_KEY_A, float3(-1, 0, 0)},
        {GLFW_KEY_D, float3(1, 0, 0)},
        {GLFW_KEY_E, float3(0, 1, 0)},
        {GLFW_KEY_Q, float3(0, -1, 0)},
        {GLFW_KEY_W, float3(0, 0, 1)},
        {GLFW_KEY_S, float3(0, 0, -1)},
    };

    void setCamera(Camera* camera) { this->camera = camera; }

    bool update(float dt)
    {
        bool changed = false;
        float3 position = camera->position;
        float3 fwd = camera->fwd;
        float3 up = camera->up;
        float3 right = camera->right;

        // Move
        if (length(moveDelta) > 0)
        {
            float3 offset = right * moveDelta.x;
            offset += up * moveDelta.y;
            offset += fwd * moveDelta.z;
            float factor = shiftDown ? kMoveShiftFactor : 1.0f;
            offset *= moveSpeed * factor * dt;
            position += offset;
            changed = true;
        }

        // Rotate
        if (length(rotateDelta) > 0)
        {
            float yaw = std::atan2(fwd.z, fwd.x);
            float pitch = std::asin(fwd.y);
            yaw += rotateSpeed * rotateDelta.x;
            pitch -= rotateSpeed * rotateDelta.y;
            fwd = float3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch));
            rotateDelta = float2();
            changed = true;
        }

        if (changed)
        {
            camera->position = position;
            camera->target = position + fwd;
            camera->up = float3(0, 1, 0);
            camera->recompute();
        }

        return changed;
    }

    void onKey(int key, int action, int mods)
    {
        if (action == GLFW_PRESS || action == GLFW_RELEASE)
        {
            bool down = action == GLFW_PRESS;
            if (key == GLFW_KEY_A || key == GLFW_KEY_D || key == GLFW_KEY_W || key == GLFW_KEY_S || key == GLFW_KEY_Q ||
                key == GLFW_KEY_E)
            {
                keyState[key] = down;
            }
            else if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT)
            {
                shiftDown = down;
            }
        }
        moveDelta = float3(0.f);
        for (auto& [key, state] : keyState)
        {
            if (state)
            {
                moveDelta += kMoveDirection.at(key);
            }
        }
    }

    void onMouseButton(int button, int action, int mods)
    {
        if (button == GLFW_MOUSE_BUTTON_LEFT)
        {
            if (action == GLFW_PRESS)
            {
                mouseDown = true;
            }
            else if (action == GLFW_RELEASE)
            {
                mouseDown = false;
            }
        }
    }

    void onMousePosition(float x, float y)
    {
        float2 pos = float2(x, y);
        if (mouseDown)
        {
            rotateDelta += pos - mousePos;
        }
        mousePos = pos;
    }
};

struct Material
{
    float3 baseColor;

    Material(float3 baseColor = float3(0.5))
        : baseColor(baseColor)
    {
    }
};

struct Mesh
{
    struct Vertex
    {
        float3 position;
        float3 normal;
        float2 uv;
    };
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    Mesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices)
        : vertices(std::move(vertices))
        , indices(std::move(indices))
    {
    }

    uint32_t vertexCount() const { return (uint32_t)(vertices.size()); }
    uint32_t indexCount() const { return (uint32_t)(indices.size()); }

    static Mesh createQuad(float2 size = float2(1))
    {
        std::vector<Vertex> vertices{
            // position, normal, uv
            {{-0.5, 0, -0.5}, {0, 1, 0}, {0, 0}},
            {{+0.5, 0, -0.5}, {0, 1, 0}, {1, 0}},
            {{-0.5, 0, +0.5}, {0, 1, 0}, {0, 1}},
            {{+0.5, 0, +0.5}, {0, 1, 0}, {1, 1}},
        };
        for (auto& vertex : vertices)
        {
            vertex.position *= float3(size.x, 0.f, size.y);
        }
        std::vector<uint32_t> indices{
            2,
            1,
            0,
            1,
            2,
            3,
        };
        return Mesh(vertices, indices);
    }

    static Mesh createCube(float3 size = float3(1))
    {
        std::vector<Vertex> vertices{
            // position, normal, uv
            // left
            {{-0.5, -0.5, -0.5}, {0, -1, 0}, {0, 0}},
            {{-0.5, -0.5, +0.5}, {0, -1, 0}, {1, 0}},
            {{+0.5, -0.5, +0.5}, {0, -1, 0}, {1, 1}},
            {{+0.5, -0.5, -0.5}, {0, -1, 0}, {0, 1}},
            // right
            {{-0.5, +0.5, +0.5}, {0, +1, 0}, {0, 0}},
            {{-0.5, +0.5, -0.5}, {0, +1, 0}, {1, 0}},
            {{+0.5, +0.5, -0.5}, {0, +1, 0}, {1, 1}},
            {{+0.5, +0.5, +0.5}, {0, +1, 0}, {0, 1}},
            // back
            {{-0.5, +0.5, -0.5}, {0, 0, -1}, {0, 0}},
            {{-0.5, -0.5, -0.5}, {0, 0, -1}, {1, 0}},
            {{+0.5, -0.5, -0.5}, {0, 0, -1}, {1, 1}},
            {{+0.5, +0.5, -0.5}, {0, 0, -1}, {0, 1}},
            // front
            {{+0.5, +0.5, +0.5}, {0, 0, +1}, {0, 0}},
            {{+0.5, -0.5, +0.5}, {0, 0, +1}, {1, 0}},
            {{-0.5, -0.5, +0.5}, {0, 0, +1}, {1, 1}},
            {{-0.5, +0.5, +0.5}, {0, 0, +1}, {0, 1}},
            // bottom
            {{-0.5, +0.5, +0.5}, {-1, 0, 0}, {0, 0}},
            {{-0.5, -0.5, +0.5}, {-1, 0, 0}, {1, 0}},
            {{-0.5, -0.5, -0.5}, {-1, 0, 0}, {1, 1}},
            {{-0.5, +0.5, -0.5}, {-1, 0, 0}, {0, 1}},
            // top
            {{+0.5, +0.5, -0.5}, {+1, 0, 0}, {0, 0}},
            {{+0.5, -0.5, -0.5}, {+1, 0, 0}, {1, 0}},
            {{+0.5, -0.5, +0.5}, {+1, 0, 0}, {1, 1}},
            {{+0.5, +0.5, +0.5}, {+1, 0, 0}, {0, 1}},
        };
        for (auto& vertex : vertices)
        {
            vertex.position *= size;
        }
        std::vector<uint32_t> indices{
            0,  2,  1,  0,  3,  2,  4,  6,  5,  4,  7,  6,  8,  10, 9,  8,  11, 10,
            12, 14, 13, 12, 15, 14, 16, 18, 17, 16, 19, 18, 20, 22, 21, 20, 23, 22,
        };
        return Mesh(vertices, indices);
    }
};

struct Transform
{
    float3 translation{0.f};
    float3 scaling{1.f};
    float3 rotation{0.f};

    float4x4 getMatrix() const
    {
        float4x4 T = translation_matrix(translation);
        float4x4 S = scaling_matrix(scaling);
        float4 Rx = rotation_quat(float3(1, 0, 0), rotation.x);
        float4 Ry = rotation_quat(float3(0, 1, 0), rotation.y);
        float4 Rz = rotation_quat(float3(0, 0, 1), rotation.z);
        float4x4 R = rotation_matrix(qmul(qmul(Rz, Ry), Rx));
        float4x4 M = mul(mul(T, R), S);
        return M;
    }
};

struct Stage
{
    Camera camera;
    std::vector<Material> materials;
    std::vector<Mesh> meshes;
    std::vector<Transform> transforms;
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> instances;

    uint32_t addMaterial(const Material& material)
    {
        uint32_t materialID = uint32_t(materials.size());
        materials.push_back(material);
        return materialID;
    }

    uint32_t addMesh(const Mesh& mesh)
    {
        uint32_t meshID = uint32_t(meshes.size());
        meshes.push_back(mesh);
        return meshID;
    }

    uint32_t addTransform(const Transform& transform)
    {
        uint32_t transformID = uint32_t(transforms.size());
        transforms.push_back(transform);
        return transformID;
    }

    uint32_t addInstance(uint32_t meshID, uint32_t materialID, uint32_t transformID)
    {
        uint32_t instanceID = uint32_t(instances.size());
        instances.push_back(std::make_tuple(meshID, materialID, transformID));
        return instanceID;
    }

    void initialize()
    {
        RNG rng;

        camera.target = float3(0, 1, 0);
        camera.position = float3(2, 1, 2);

        uint32_t floorMaterial = addMaterial(Material(float3(0.5)));
        uint32_t floorMesh = addMesh(Mesh::createQuad(float2(5, 5)));
        uint32_t floorTransform = addTransform(Transform());
        addInstance(floorMesh, floorMaterial, floorTransform);

        std::vector<uint32_t> cubeMaterials;
        for (uint32_t i = 0; i < 50; ++i)
            cubeMaterials.push_back(addMaterial(Material(rng.nextFloat3())));
        uint32_t cubeMesh = addMesh(Mesh::createCube(float3(0.1)));
        for (uint32_t i = 0; i < 1000; ++i)
        {
            Transform transform;
            transform.translation = rng.nextFloat3() * 2.f - 1.f;
            transform.translation.y += 1;
            transform.scaling = rng.nextFloat3() + 0.5f;
            transform.rotation = rng.nextFloat3() * 10.f;
            uint32_t cubeTransform = addTransform(transform);
            addInstance(cubeMesh, cubeMaterials[i % cubeMaterials.size()], cubeTransform);
        }
    }
};

struct Scene
{
    struct MaterialDesc
    {
        float3 baseColor;
    };

    struct MeshDesc
    {
        uint32_t vertexCount;
        uint32_t indexCount;
        uint32_t vertexOffset;
        uint32_t indexOffset;
    };

    struct InstanceDesc
    {
        uint32_t meshID;
        uint32_t materialID;
        uint32_t transformID;
    };

    ComPtr<IDevice> device;
    const Stage* stage;
    const Camera* camera;

    std::vector<MaterialDesc> materialDescs;
    ComPtr<IBuffer> materialDescsBuffer;
    std::vector<MeshDesc> meshDescs;
    ComPtr<IBuffer> meshDescsBuffer;
    std::vector<InstanceDesc> instanceDescs;
    ComPtr<IBuffer> instanceDescsBuffer;
    ComPtr<IBuffer> vertexBuffer;
    ComPtr<IBuffer> indexBuffer;
    std::vector<float4x4> transforms;
    std::vector<float4x4> inverseTransposeTransforms;
    ComPtr<IBuffer> transformsBuffer;
    ComPtr<IBuffer> inverseTransposeTransformsBuffer;
    std::vector<ComPtr<IAccelerationStructure>> blases;
    ComPtr<IAccelerationStructure> tlas;

    Result initialize(IDevice* device, const Stage& stage)
    {
        this->device = device;
        this->stage = &stage;
        this->camera = &stage.camera;

        // Prepare material descriptors
        materialDescs.resize(stage.materials.size());
        for (size_t i = 0; i < stage.materials.size(); ++i)
        {
            materialDescs[i].baseColor = stage.materials[i].baseColor;
        }
        {
            BufferDesc desc = {};
            desc.size = materialDescs.size() * sizeof(MaterialDesc);
            desc.usage = BufferUsage::ShaderResource;
            desc.label = "materialDescsBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, materialDescs.data(), materialDescsBuffer.writeRef()));
        }

        // Prepare mesh descriptors
        meshDescs.reserve(stage.meshes.size());
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        for (const Mesh& mesh : stage.meshes)
        {
            MeshDesc desc = {};
            desc.vertexCount = mesh.vertexCount();
            desc.indexCount = mesh.indexCount();
            desc.vertexOffset = vertexCount;
            desc.indexOffset = indexCount;
            meshDescs.push_back(desc);
            vertexCount += mesh.vertexCount();
            indexCount += mesh.indexCount();
        }

        // Prepare instance descriptors
        instanceDescs.reserve(stage.instances.size());
        for (const auto& [meshID, materialID, transformID] : stage.instances)
        {
            InstanceDesc desc = {};
            desc.meshID = meshID;
            desc.materialID = materialID;
            desc.transformID = transformID;
            instanceDescs.push_back(desc);
        }

        // Create vertex and index buffers
        std::vector<Mesh::Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(vertexCount);
        indices.reserve(indexCount);
        for (const Mesh& mesh : stage.meshes)
        {
            vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
            indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
        }
        {
            BufferDesc desc = {};
            desc.size = vertices.size() * sizeof(Mesh::Vertex);
            desc.usage = BufferUsage::ShaderResource;
            desc.label = "vertexBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, vertices.data(), vertexBuffer.writeRef()));
        }
        {
            BufferDesc desc = {};
            desc.size = indices.size() * sizeof(uint32_t);
            desc.usage = BufferUsage::ShaderResource;
            desc.label = "indexBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, indices.data(), indexBuffer.writeRef()));
        }
        {
            BufferDesc desc = {};
            desc.size = meshDescs.size() * sizeof(MeshDesc);
            desc.usage = BufferUsage::ShaderResource;
            desc.label = "meshDescsBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, meshDescs.data(), meshDescsBuffer.writeRef()));
        }
        {
            BufferDesc desc = {};
            desc.size = instanceDescs.size() * sizeof(InstanceDesc);
            desc.usage = BufferUsage::ShaderResource;
            desc.label = "instanceDescsBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, instanceDescs.data(), instanceDescsBuffer.writeRef()));
        }

        // Prepare transforms
        transforms.resize(stage.transforms.size());
        for (size_t i = 0; i < stage.transforms.size(); ++i)
        {
            transforms[i] = transpose(stage.transforms[i].getMatrix());
        }
        inverseTransposeTransforms.resize(transforms.size());
        for (size_t i = 0; i < transforms.size(); ++i)
        {
            inverseTransposeTransforms[i] = transpose(inverse(transforms[i]));
        }
        {
            BufferDesc desc = {};
            desc.size = transforms.size() * sizeof(float4x4);
            desc.usage = BufferUsage::ShaderResource;
            desc.label = "transformsBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, transforms.data(), transformsBuffer.writeRef()));
        }
        {
            BufferDesc desc = {};
            desc.size = inverseTransposeTransforms.size() * sizeof(float4x4);
            desc.usage = BufferUsage::ShaderResource;
            desc.label = "inverseTransposeTransformsBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(
                desc,
                inverseTransposeTransforms.data(),
                inverseTransposeTransformsBuffer.writeRef()
            ));
        }

        // Build BLASes
        for (const MeshDesc& meshDesc : meshDescs)
        {
            ComPtr<IAccelerationStructure> blas;
            SLANG_RETURN_ON_FAIL(buildBLAS(meshDesc, blas.writeRef()));
            blases.push_back(blas);
        }

        // Build TLAS
        SLANG_RETURN_ON_FAIL(buildTLAS(tlas.writeRef()));

        return SLANG_OK;
    }

    Result buildBLAS(const MeshDesc& meshDesc, IAccelerationStructure** outBLAS)
    {
        AccelerationStructureBuildInput buildInput = {};
        buildInput.type = AccelerationStructureBuildInputType::Triangles;
        AccelerationStructureBuildInputTriangles& triangles = buildInput.triangles;
        triangles.vertexBuffers[0] = BufferOffsetPair(vertexBuffer, meshDesc.vertexOffset * sizeof(Mesh::Vertex));
        triangles.vertexBufferCount = 1;
        triangles.vertexFormat = Format::RGB32Float;
        triangles.vertexCount = meshDesc.vertexCount;
        triangles.vertexStride = sizeof(Mesh::Vertex);
        triangles.indexBuffer = BufferOffsetPair(indexBuffer, meshDesc.indexOffset * sizeof(uint32_t));
        triangles.indexFormat = IndexFormat::Uint32;
        triangles.indexCount = meshDesc.indexCount;
        triangles.flags = AccelerationStructureGeometryFlags::Opaque;

        AccelerationStructureBuildDesc buildDesc = {};
        buildDesc.inputs = &buildInput;
        buildDesc.inputCount = 1;

        AccelerationStructureSizes sizes = {};
        SLANG_RETURN_ON_FAIL(device->getAccelerationStructureSizes(buildDesc, &sizes));

        AccelerationStructureDesc asDesc = {};
        asDesc.size = sizes.accelerationStructureSize;
        asDesc.label = "blas";
        SLANG_RETURN_ON_FAIL(device->createAccelerationStructure(asDesc, outBLAS));

        ComPtr<IBuffer> blasScratchBuffer;
        {
            BufferDesc desc = {};
            desc.size = sizes.scratchSize;
            desc.usage = BufferUsage::UnorderedAccess;
            desc.label = "blasScratchBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, nullptr, blasScratchBuffer.writeRef()));
        }

        ComPtr<ICommandQueue> queue;
        SLANG_RETURN_ON_FAIL(device->getQueue(QueueType::Graphics, queue.writeRef()));

        ComPtr<ICommandEncoder> commandEncoder;
        SLANG_RETURN_ON_FAIL(queue->createCommandEncoder(commandEncoder.writeRef()));
        commandEncoder->buildAccelerationStructure(buildDesc, *outBLAS, nullptr, blasScratchBuffer, 0, nullptr);
        ComPtr<ICommandBuffer> commandBuffer;
        SLANG_RETURN_ON_FAIL(commandEncoder->finish(commandBuffer.writeRef()));
        queue->submit(commandBuffer);

        return SLANG_OK;
    }

    Result buildTLAS(IAccelerationStructure** outTLAS)
    {
        std::vector<AccelerationStructureInstanceDescGeneric> instanceDescsGeneric(instanceDescs.size());
        for (size_t instanceID = 0; instanceID < instanceDescs.size(); ++instanceID)
        {
            const InstanceDesc& instanceDesc = instanceDescs[instanceID];
            AccelerationStructureInstanceDescGeneric& instanceDescGeneric = instanceDescsGeneric[instanceID];
            memcpy(instanceDescGeneric.transform, &transforms[instanceDesc.transformID], sizeof(float3x4));
            instanceDescGeneric.instanceID = (uint32_t)instanceID;
            instanceDescGeneric.instanceMask = 0xFF;
            instanceDescGeneric.instanceContributionToHitGroupIndex = 0;
            instanceDescGeneric.flags = AccelerationStructureInstanceFlags::None;
            instanceDescGeneric.accelerationStructure = blases[instanceDesc.meshID]->getHandle();
        }

        AccelerationStructureInstanceDescType instanceDescType = getAccelerationStructureInstanceDescType(device);
        size_t instanceDescSize = getAccelerationStructureInstanceDescSize(instanceDescType);
        std::vector<uint8_t> instanceDescsData(instanceDescsGeneric.size() * instanceDescSize);
        convertAccelerationStructureInstanceDescs(
            instanceDescsGeneric.size(),
            instanceDescType,
            instanceDescsData.data(),
            instanceDescSize,
            instanceDescsGeneric.data(),
            getAccelerationStructureInstanceDescSize(AccelerationStructureInstanceDescType::Generic)
        );

        ComPtr<IBuffer> instanceDescsBuffer;
        {
            BufferDesc desc = {};
            desc.size = (uint32_t)instanceDescsData.size();
            desc.usage = BufferUsage::AccelerationStructureBuildInput;
            desc.label = "tlasInstanceDescsBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, instanceDescsData.data(), instanceDescsBuffer.writeRef()));
        }

        AccelerationStructureBuildInput buildInput = {};
        buildInput.type = AccelerationStructureBuildInputType::Instances;
        AccelerationStructureBuildInputInstances& instances = buildInput.instances;
        instances.instanceBuffer = instanceDescsBuffer;
        instances.instanceCount = (uint32_t)instanceDescsGeneric.size();
        instances.instanceStride = (uint32_t)instanceDescSize;

        AccelerationStructureBuildDesc buildDesc = {};
        buildDesc.inputs = &buildInput;
        buildDesc.inputCount = 1;

        AccelerationStructureSizes sizes = {};
        SLANG_RETURN_ON_FAIL(device->getAccelerationStructureSizes(buildDesc, &sizes));

        AccelerationStructureDesc asDesc = {};
        asDesc.size = sizes.accelerationStructureSize;
        asDesc.label = "tlas";
        SLANG_RETURN_ON_FAIL(device->createAccelerationStructure(asDesc, outTLAS));

        ComPtr<IBuffer> tlasScratchBuffer;
        {
            BufferDesc desc = {};
            desc.size = sizes.scratchSize;
            desc.usage = BufferUsage::UnorderedAccess;
            desc.label = "tlasScratchBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, nullptr, tlasScratchBuffer.writeRef()));
        }

        ComPtr<ICommandQueue> queue;
        SLANG_RETURN_ON_FAIL(device->getQueue(QueueType::Graphics, queue.writeRef()));

        ComPtr<ICommandEncoder> commandEncoder;
        SLANG_RETURN_ON_FAIL(queue->createCommandEncoder(commandEncoder.writeRef()));
        commandEncoder->buildAccelerationStructure(buildDesc, *outTLAS, nullptr, tlasScratchBuffer, 0, nullptr);
        ComPtr<ICommandBuffer> commandBuffer;
        SLANG_RETURN_ON_FAIL(commandEncoder->finish(commandBuffer.writeRef()));
        queue->submit(commandBuffer);

        return SLANG_OK;
    }

    void bind(ShaderCursor cursor) const
    {
        cursor["tlas"].setBinding(tlas);
        cursor["materialDescs"].setBinding(materialDescsBuffer);
        cursor["meshDescs"].setBinding(meshDescsBuffer);
        cursor["instanceDescs"].setBinding(instanceDescsBuffer);
        cursor["vertices"].setBinding(vertexBuffer);
        cursor["indices"].setBinding(indexBuffer);
        cursor["transforms"].setBinding(transformsBuffer);
        cursor["inverseTransposeTransforms"].setBinding(inverseTransposeTransformsBuffer);
        camera->bind(cursor["camera"]);
    }
};

struct PathTracer
{
    ComPtr<IDevice> device;
    Scene* scene;
    ComPtr<IShaderProgram> program;
    ComPtr<IComputePipeline> computePipeline;
    ComPtr<IRayTracingPipeline> rayTracingPipeline;
    ComPtr<IShaderTable> shaderTable;

    Result initialize(IDevice* device, Scene* scene)
    {
        this->device = device;
        this->scene = scene;

        if (USE_RAYTRACING_PIPELINE)
        {
            ComPtr<IShaderProgram> program;
            SLANG_RETURN_ON_FAIL(createProgram(
                device,
                "path-tracer.slang",
                {"renderRaygen", "mainClosestHit", "mainMiss"},
                program.writeRef()
            ));

            HitGroupDesc hitGroupDescs[1] = {};
            hitGroupDescs[0].hitGroupName = "default";
            hitGroupDescs[0].closestHitEntryPoint = "mainClosestHit";
            RayTracingPipelineDesc rayTracingPipelineDesc = {};
            rayTracingPipelineDesc.program = program;
            rayTracingPipelineDesc.hitGroupCount = SLANG_COUNT_OF(hitGroupDescs);
            rayTracingPipelineDesc.hitGroups = hitGroupDescs;
            rayTracingPipelineDesc.maxRecursion = 6;
            rayTracingPipelineDesc.maxRayPayloadSize = 128;
            rayTracingPipelineDesc.maxAttributeSizeInBytes = 8;
            rayTracingPipelineDesc.flags = RayTracingPipelineFlags::None;
            SLANG_RETURN_ON_FAIL(
                device->createRayTracingPipeline(rayTracingPipelineDesc, rayTracingPipeline.writeRef())
            );

            const char* rayGenEntryPoints[] = {"renderRaygen"};
            const char* missEntryPoints[] = {"mainMiss"};
            const char* hitGroupNames[] = {"default"};
            ShaderTableDesc shaderTableDesc = {};
            shaderTableDesc.rayGenShaderCount = SLANG_COUNT_OF(rayGenEntryPoints);
            shaderTableDesc.rayGenShaderEntryPointNames = rayGenEntryPoints;
            shaderTableDesc.missShaderCount = SLANG_COUNT_OF(missEntryPoints);
            shaderTableDesc.missShaderEntryPointNames = missEntryPoints;
            shaderTableDesc.hitGroupCount = SLANG_COUNT_OF(hitGroupNames);
            shaderTableDesc.hitGroupNames = hitGroupNames;
            shaderTableDesc.program = program;
            SLANG_RETURN_ON_FAIL(device->createShaderTable(shaderTableDesc, shaderTable.writeRef()));
        }
        else
        {
            SLANG_RETURN_ON_FAIL(
                createComputePipeline(device, "path-tracer.slang", "renderCompute", computePipeline.writeRef())
            );
        }

        return SLANG_OK;
    }

    Result execute(ICommandEncoder* commandEncoder, ITexture* output, uint32_t frame)
    {
        if (USE_RAYTRACING_PIPELINE)
        {
            IRayTracingPassEncoder* passEncoder = commandEncoder->beginRayTracingPass();
            IShaderObject* shaderObject = passEncoder->bindPipeline(rayTracingPipeline, shaderTable);
            ShaderCursor cursor = ShaderCursor(shaderObject);
            scene->bind(cursor["g_scene"]);
            cursor = ShaderCursor(shaderObject->getEntryPoint(0));
            cursor["output"].setBinding(output);
            cursor["frame"].setData(frame);
            Extent3D size = output->getDesc().size;
            passEncoder->dispatchRays(0, size.width, size.height, 1);
            passEncoder->end();
        }
        else
        {
            IComputePassEncoder* passEncoder = commandEncoder->beginComputePass();
            IShaderObject* shaderObject = passEncoder->bindPipeline(computePipeline);
            ShaderCursor cursor = ShaderCursor(shaderObject);
            scene->bind(cursor["g_scene"]);
            cursor = ShaderCursor(shaderObject->getEntryPoint(0));
            cursor["output"].setBinding(output);
            cursor["frame"].setData(frame);
            Extent3D size = output->getDesc().size;
            passEncoder->dispatchCompute(divRoundUp(size.width, 8u), divRoundUp(size.height, 8u), 1);
            passEncoder->end();
        }

        return SLANG_OK;
    }
};

struct Accumulator
{
    ComPtr<IDevice> device;
    ComPtr<IComputePipeline> pipeline;
    ComPtr<ITexture> accumulator;

    Result initialize(IDevice* device)
    {
        this->device = device;
        SLANG_RETURN_ON_FAIL(createComputePipeline(device, "path-tracer.slang", "accumulate", pipeline.writeRef()));
        return SLANG_OK;
    }

    Result execute(ICommandEncoder* commandEncoder, ITexture* input, ITexture* output, bool reset = false)
    {
        if (!accumulator || accumulator->getDesc().size.width != input->getDesc().size.width ||
            accumulator->getDesc().size.height != input->getDesc().size.height)
        {
            TextureDesc desc = {};
            desc.type = TextureType::Texture2D;
            desc.size = input->getDesc().size;
            desc.format = Format::RGBA32Float;
            desc.usage = TextureUsage::ShaderResource | TextureUsage::UnorderedAccess;
            desc.label = "accumulator";
            SLANG_RETURN_ON_FAIL(device->createTexture(desc, nullptr, accumulator.writeRef()));
        }
        IComputePassEncoder* passEncoder = commandEncoder->beginComputePass();
        IShaderObject* shaderObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor cursor = ShaderCursor(shaderObject->getEntryPoint(0));
        cursor["input"].setBinding(input);
        cursor["output"].setBinding(output);
        cursor["accumulator"].setBinding(accumulator);
        cursor["reset"].setData(reset ? 1u : 0u);
        Extent3D size = input->getDesc().size;
        passEncoder->dispatchCompute(divRoundUp(size.width, 8u), divRoundUp(size.height, 8u), 1);
        passEncoder->end();
        return SLANG_OK;
    }
};

struct ToneMapper
{
    ComPtr<IComputePipeline> pipeline;

    Result initialize(IDevice* device)
    {
        SLANG_RETURN_ON_FAIL(createComputePipeline(device, "path-tracer.slang", "tonemap", pipeline.writeRef()));
        return SLANG_OK;
    }

    Result execute(ICommandEncoder* commandEncoder, ITexture* input, ITexture* output)
    {
        IComputePassEncoder* passEncoder = commandEncoder->beginComputePass();
        IShaderObject* shaderObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor cursor = ShaderCursor(shaderObject->getEntryPoint(0));
        cursor["input"].setBinding(input);
        cursor["output"].setBinding(output);
        Extent3D size = input->getDesc().size;
        passEncoder->dispatchCompute(divRoundUp(size.width, 8u), divRoundUp(size.height, 8u), 1);
        passEncoder->end();
        return SLANG_OK;
    }
};

class ExamplePathTracer : public ExampleBase
{
public:
    Result init(DeviceType deviceType) override
    {
        std::vector<Feature> requiredFeatures = {
            Feature::Surface,
            Feature::AccelerationStructure,
            USE_RAYTRACING_PIPELINE ? Feature::RayTracing : Feature::RayQuery,
        };
        std::vector<std::pair<std::string, std::string>> preprocessorMacros = {
            {"USE_RAYTRACING_PIPELINE", USE_RAYTRACING_PIPELINE ? "1" : "0"}
        };
        SLANG_RETURN_ON_FAIL(createDevice(deviceType, requiredFeatures, preprocessorMacros, m_device.writeRef()));
        SLANG_RETURN_ON_FAIL(createWindow(m_device, "PathTracer"));
        SLANG_RETURN_ON_FAIL(createSurface(m_device, Format::Undefined, m_surface.writeRef()));

        SLANG_RETURN_ON_FAIL(m_device->getQueue(QueueType::Graphics, m_queue.writeRef()));

        m_blitter = std::make_unique<Blitter>(m_device);

        m_stage.initialize();
        SLANG_RETURN_ON_FAIL(m_scene.initialize(m_device, m_stage));
        m_cameraController.setCamera(&m_stage.camera);
        SLANG_RETURN_ON_FAIL(m_pathTracer.initialize(m_device, &m_scene));
        SLANG_RETURN_ON_FAIL(m_accumulator.initialize(m_device));
        SLANG_RETURN_ON_FAIL(m_toneMapper.initialize(m_device));
        return SLANG_OK;
    }

    virtual void shutdown() override
    {
        m_queue->waitOnHost();
        m_queue.setNull();
        m_blitter.reset();
        m_surface.setNull();
        m_device.setNull();
    }

    virtual Result update(double time) override
    {
        if (m_time == 0.0)
        {
            m_time = time;
        }
        m_timeDelta = time - m_time;
        m_time = time;

        if (m_cameraController.update(m_timeDelta))
        {
            m_frame = 0;
        }
        return SLANG_OK;
    }

    virtual Result draw() override
    {
        // Skip rendering if surface is not configured (eg. when window is minimized)
        if (!m_surface->getConfig())
        {
            return SLANG_OK;
        }

        // Acquire next image from the surface
        ComPtr<ITexture> image;
        m_surface->acquireNextImage(image.writeRef());
        if (!image)
        {
            return SLANG_OK;
        }

        uint32_t width = image->getDesc().size.width;
        uint32_t height = image->getDesc().size.height;

        // Create or resize auxiliary textures if needed
        if (!m_renderTexture || m_renderTexture->getDesc().size.width != width ||
            m_renderTexture->getDesc().size.height != height)
        {
            TextureDesc desc = {};
            desc.type = TextureType::Texture2D;
            desc.size = {width, height, 1};
            desc.format = Format::RGBA32Float;
            desc.usage = TextureUsage::ShaderResource | TextureUsage::UnorderedAccess;
            desc.label = "renderTexture";
            SLANG_RETURN_ON_FAIL(m_device->createTexture(desc, nullptr, m_renderTexture.writeRef()));
            desc.label = "accumTexture";
            SLANG_RETURN_ON_FAIL(m_device->createTexture(desc, nullptr, m_accumTexture.writeRef()));
            desc.label = "outputTexture";
            SLANG_RETURN_ON_FAIL(m_device->createTexture(desc, nullptr, m_outputTexture.writeRef()));
            m_frame = 0;
        }

        m_stage.camera.width = width;
        m_stage.camera.height = height;
        m_stage.camera.recompute();

        // Start command encoding
        ComPtr<ICommandEncoder> commandEncoder;
        SLANG_RETURN_ON_FAIL(m_queue->createCommandEncoder(commandEncoder.writeRef()));

        SLANG_RETURN_ON_FAIL(m_pathTracer.execute(commandEncoder, m_renderTexture, m_frame));
        SLANG_RETURN_ON_FAIL(m_accumulator.execute(commandEncoder, m_renderTexture, m_accumTexture, m_frame == 0));
        SLANG_RETURN_ON_FAIL(m_toneMapper.execute(commandEncoder, m_accumTexture, m_outputTexture));

        // Blit result to the surface image
        SLANG_RETURN_ON_FAIL(m_blitter->blit(image, m_outputTexture, commandEncoder));

        // Submit command buffer
        m_queue->submit(commandEncoder->finish());

        m_frame += 1;

        // Present the surface
        return m_surface->present();
    }

    virtual void onResize(int width, int height, int framebufferWidth, int framebufferHeight) override
    {
        // Wait for GPU to be idle before resizing
        m_device->getQueue(QueueType::Graphics)->waitOnHost();
        // Configure or unconfigure the surface based on the new framebuffer size
        if (framebufferWidth > 0 && framebufferHeight > 0)
        {
            SurfaceConfig surfaceConfig;
            surfaceConfig.width = framebufferWidth;
            surfaceConfig.height = framebufferHeight;
            m_surface->configure(surfaceConfig);
        }
        else
        {
            m_surface->unconfigure();
        }
    }

    virtual void onMousePosition(float x, float y) override { m_cameraController.onMousePosition(x, y); }

    virtual void onMouseButton(int button, int action, int mods) override
    {
        m_cameraController.onMouseButton(button, action, mods);
    }

    virtual void onKey(int key, int scancode, int action, int mods) override
    {
        m_cameraController.onKey(key, action, mods);
    }

    ComPtr<IDevice> m_device;
    ComPtr<ISurface> m_surface;
    ComPtr<ICommandQueue> m_queue;
    std::unique_ptr<Blitter> m_blitter;

    Stage m_stage;
    Scene m_scene;
    CameraController m_cameraController;
    PathTracer m_pathTracer;
    Accumulator m_accumulator;
    ToneMapper m_toneMapper;
    ComPtr<ITexture> m_renderTexture;
    ComPtr<ITexture> m_accumTexture;
    ComPtr<ITexture> m_outputTexture;

    double m_time = 0.0;
    double m_timeDelta = 0.0;
    uint32_t m_frame = 0;
};

EXAMPLE_MAIN(ExamplePathTracer)
