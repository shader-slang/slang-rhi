// SPDX-FileCopyrightText: 2015, 2020-2021 The Khronos Group Inc.
// SPDX-FileCopyrightText: 2020, 2023 Advanced Micro Devices, Inc.
// SPDX-FileCopyrightText: 2021 Valve Corporation
// SPDX-License-Identifier: Apache-2.0
//
// The case selection and semantics are adapted from the Khronos VK-GL-CTS ray-query suite:
// https://github.com/KhronosGroup/VK-GL-CTS/tree/main/external/vulkancts/modules/vulkan/ray_query

#include "testing.h"

#include <slang-rhi/acceleration-structure-utils.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace rhi;
using namespace rhi::testing;

namespace {

enum class CaseID : uint32_t
{
    ProceduralObjectBehindBoundingBoxes,
    ProceduralTriangleInBetween,
    TraversalGenerateAabbs,
    TraversalGenerateTriangles,
    TraversalSkipAabbs,
    TraversalSkipTriangles,
    FlagsCullBackFacingTriangles,
    FlagsCullFrontFacingTriangles,
    FlagsForceNonOpaque,
    BuiltinRayQueryTerminate,
    FlagsCullNonOpaque,
    FlagsCullOpaque,
    FlagsSkipAabbs,
    FlagsSkipTriangles,
    FlagsTerminateFirstAabbs,
    FlagsTerminateFirstTriangles,
    WatertightNoMissAabbs,
    WatertightSingleHitTriangles,
    InsideAabbRayEndInside,
    DirectionLengthTriangles,
    BarycentricCoordinates,
    MultipleRayQueries,
    StressAabbs,
    StressTriangles,
    NonUniformArgsNoMiss,
    NonUniformArgsCullMaskMiss,
    BuiltinGeometryIndexCandidate,
    BuiltinPrimitiveIDAabbs,
    BuiltinInstanceCustomIndex,
    BuiltinObjectToWorld,
    Count,
};

enum class SceneKind
{
    SingleOpaqueTriangle,
    MaskedOpaqueTriangle,
    SingleNonOpaqueTriangle,
    FrontFacingOpaqueTriangle,
    SingleAabb,
    ProceduralWall,
    TriangleInBetween,
    TwoAabbs,
    TwoTriangles,
    SharedFaceAabbs,
    SharedEdgeTriangles,
    DirectionTriangle,
    StressAabbs,
    StressTriangles,
    GeometryIndex,
    PrimitiveIDAabbs,
    InstanceCustomIndex,
    ObjectToWorld,
};

enum RayFlag : uint32_t
{
    ForceOpaque = 0x01,
    ForceNonOpaque = 0x02,
    AcceptFirstHitAndEndSearch = 0x04,
    CullBackFacingTriangles = 0x10,
    CullFrontFacingTriangles = 0x20,
    CullOpaque = 0x40,
    CullNonOpaque = 0x80,
    SkipTriangles = 0x100,
    SkipProceduralPrimitives = 0x200,
};

enum Behavior : uint32_t
{
    CommitTriangles = 0x01,
    CommitProcedural = 0x02,
    AbortAfterCandidate = 0x04,
    CaptureFirstCandidateMetadata = 0x08,
    CaptureCommittedMetadata = 0x10,
    CaptureBarycentrics = 0x20,
    CaptureTransform = 0x40,
};

enum Mode : uint32_t
{
    Standard = 0,
    MultipleQueries = 1,
};

enum CommittedStatus : uint32_t
{
    CommittedNothing = 0,
    CommittedTriangle = 1,
    CommittedProcedural = 2,
};

struct CaseConfig
{
    uint32_t mode;
    uint32_t rayFlags;
    uint32_t instanceMask;
    uint32_t behavior;
    float rayOriginAndTMin[4];
    float rayDirectionAndTMax[4];
    float proceduralTBase;
    float proceduralTPrimitiveStep;
    float proceduralTGeometryStep;
    uint32_t reserved;
};

struct TestResult
{
    uint32_t committedStatus;
    uint32_t candidateCount;
    uint32_t triangleCandidateCount;
    uint32_t proceduralCandidateCount;

    uint32_t firstCandidatePrimitiveIndex;
    uint32_t firstCandidateGeometryIndex;
    uint32_t firstCandidateInstanceID;
    uint32_t firstCandidateInstanceIndex;

    uint32_t committedPrimitiveIndex;
    uint32_t committedGeometryIndex;
    uint32_t committedInstanceID;
    uint32_t committedInstanceIndex;

    float committedT;
    uint32_t committedFrontFace;
    float committedBarycentricU;
    float committedBarycentricV;

    float transformM00;
    float transformM03;
    float transformM11;
    float transformM23;

    uint32_t auxiliary0;
    uint32_t auxiliary1;
    uint32_t auxiliary2;
    uint32_t auxiliary3;
};

static_assert(sizeof(CaseConfig) == 64);
static_assert(sizeof(TestResult) == 96);
static_assert(uint32_t(CaseID::Count) == 30);

struct CaseSetup
{
    SceneKind scene = SceneKind::SingleOpaqueTriangle;
    CaseConfig config = {};
    TestResult expected = {};
};

struct TriangleGeometry
{
    std::vector<std::array<float, 3>> vertices;
    bool opaque = false;
};

struct AabbGeometry
{
    std::vector<AccelerationStructureAABB> bounds;
    bool opaque = false;
};

struct InstanceSpec
{
    IAccelerationStructure* bottomLevel = nullptr;
    std::array<float, 12> transform = {
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
    };
    uint32_t instanceID = 17;
    uint32_t instanceMask = 0xff;
    uint32_t contribution = 0;
    AccelerationStructureInstanceFlags flags = AccelerationStructureInstanceFlags::None;
};

TestResult makeDefaultResult()
{
    TestResult result = {};
    result.firstCandidatePrimitiveIndex = std::numeric_limits<uint32_t>::max();
    result.firstCandidateGeometryIndex = std::numeric_limits<uint32_t>::max();
    result.firstCandidateInstanceID = std::numeric_limits<uint32_t>::max();
    result.firstCandidateInstanceIndex = std::numeric_limits<uint32_t>::max();
    result.committedPrimitiveIndex = std::numeric_limits<uint32_t>::max();
    result.committedGeometryIndex = std::numeric_limits<uint32_t>::max();
    result.committedInstanceID = std::numeric_limits<uint32_t>::max();
    result.committedInstanceIndex = std::numeric_limits<uint32_t>::max();
    result.committedT = -1.0f;
    return result;
}

CaseSetup makeDefaultSetup()
{
    CaseSetup setup;
    setup.config.mode = Mode::Standard;
    setup.config.instanceMask = 0xff;
    setup.config.rayOriginAndTMin[0] = 0.25f;
    setup.config.rayOriginAndTMin[1] = 0.25f;
    setup.config.rayOriginAndTMin[2] = 0.0f;
    setup.config.rayOriginAndTMin[3] = 0.0f;
    setup.config.rayDirectionAndTMax[0] = 0.0f;
    setup.config.rayDirectionAndTMax[1] = 0.0f;
    setup.config.rayDirectionAndTMax[2] = 1.0f;
    setup.config.rayDirectionAndTMax[3] = 100.0f;
    setup.expected = makeDefaultResult();
    return setup;
}

void expectTriangleHit(CaseSetup& setup, float rayT)
{
    setup.expected.committedStatus = CommittedStatus::CommittedTriangle;
    setup.expected.committedT = rayT;
}

void expectProceduralHit(CaseSetup& setup, float rayT)
{
    setup.expected.committedStatus = CommittedStatus::CommittedProcedural;
    setup.expected.committedT = rayT;
}

CaseSetup getCaseSetup(CaseID caseID)
{
    CaseSetup setup = makeDefaultSetup();
    switch (caseID)
    {
    case CaseID::ProceduralObjectBehindBoundingBoxes:
        setup.scene = SceneKind::ProceduralWall;
        setup.config.behavior = CommitProcedural | CaptureCommittedMetadata;
        setup.config.proceduralTBase = 6.0f;
        setup.config.proceduralTPrimitiveStep = -1.0f;
        setup.expected.candidateCount = 4;
        setup.expected.proceduralCandidateCount = 4;
        expectProceduralHit(setup, 3.0f);
        setup.expected.committedPrimitiveIndex = 3;
        setup.expected.committedGeometryIndex = 0;
        setup.expected.committedInstanceID = 17;
        setup.expected.committedInstanceIndex = 0;
        break;

    case CaseID::ProceduralTriangleInBetween:
        setup.scene = SceneKind::TriangleInBetween;
        setup.config.behavior = CommitProcedural | CaptureCommittedMetadata;
        setup.config.proceduralTBase = 4.0f;
        setup.expected.candidateCount = 1;
        setup.expected.proceduralCandidateCount = 1;
        expectTriangleHit(setup, 2.0f);
        setup.expected.committedPrimitiveIndex = 0;
        setup.expected.committedGeometryIndex = 0;
        setup.expected.committedInstanceID = 23;
        setup.expected.committedInstanceIndex = 1;
        break;

    case CaseID::TraversalGenerateAabbs:
        setup.scene = SceneKind::SingleAabb;
        setup.config.behavior = CommitProcedural | CaptureFirstCandidateMetadata | CaptureCommittedMetadata;
        setup.config.proceduralTBase = 0.5f;
        setup.expected.candidateCount = 1;
        setup.expected.proceduralCandidateCount = 1;
        setup.expected.firstCandidatePrimitiveIndex = 0;
        setup.expected.firstCandidateGeometryIndex = 0;
        setup.expected.firstCandidateInstanceID = 17;
        setup.expected.firstCandidateInstanceIndex = 0;
        expectProceduralHit(setup, 0.5f);
        setup.expected.committedPrimitiveIndex = 0;
        setup.expected.committedGeometryIndex = 0;
        setup.expected.committedInstanceID = 17;
        setup.expected.committedInstanceIndex = 0;
        break;

    case CaseID::TraversalGenerateTriangles:
        setup.scene = SceneKind::SingleNonOpaqueTriangle;
        setup.config.behavior = CommitTriangles;
        setup.expected.candidateCount = 1;
        setup.expected.triangleCandidateCount = 1;
        expectTriangleHit(setup, 1.0f);
        break;

    case CaseID::TraversalSkipAabbs:
        setup.scene = SceneKind::SingleAabb;
        setup.expected.candidateCount = 1;
        setup.expected.proceduralCandidateCount = 1;
        break;

    case CaseID::TraversalSkipTriangles:
        setup.scene = SceneKind::SingleNonOpaqueTriangle;
        setup.expected.candidateCount = 1;
        setup.expected.triangleCandidateCount = 1;
        break;

    case CaseID::FlagsCullBackFacingTriangles:
        setup.scene = SceneKind::SingleOpaqueTriangle;
        setup.config.rayFlags = RayFlag::CullBackFacingTriangles;
        break;

    case CaseID::FlagsCullFrontFacingTriangles:
        setup.scene = SceneKind::FrontFacingOpaqueTriangle;
        setup.config.rayFlags = RayFlag::CullFrontFacingTriangles;
        break;

    case CaseID::FlagsForceNonOpaque:
        setup.scene = SceneKind::SingleOpaqueTriangle;
        setup.config.rayFlags = RayFlag::ForceNonOpaque;
        setup.config.behavior = CommitTriangles;
        setup.expected.candidateCount = 1;
        setup.expected.triangleCandidateCount = 1;
        expectTriangleHit(setup, 1.0f);
        break;

    case CaseID::BuiltinRayQueryTerminate:
        setup.scene = SceneKind::TwoTriangles;
        setup.config.rayFlags = RayFlag::ForceNonOpaque;
        setup.config.behavior = AbortAfterCandidate;
        setup.expected.candidateCount = 1;
        setup.expected.triangleCandidateCount = 1;
        break;

    case CaseID::FlagsCullNonOpaque:
        setup.scene = SceneKind::SingleNonOpaqueTriangle;
        setup.config.rayFlags = RayFlag::CullNonOpaque;
        break;

    case CaseID::FlagsCullOpaque:
        setup.scene = SceneKind::SingleOpaqueTriangle;
        setup.config.rayFlags = RayFlag::CullOpaque;
        break;

    case CaseID::FlagsSkipAabbs:
        setup.scene = SceneKind::SingleAabb;
        setup.config.rayFlags = RayFlag::SkipProceduralPrimitives;
        break;

    case CaseID::FlagsSkipTriangles:
        setup.scene = SceneKind::SingleOpaqueTriangle;
        setup.config.rayFlags = RayFlag::SkipTriangles;
        break;

    case CaseID::FlagsTerminateFirstAabbs:
        setup.scene = SceneKind::TwoAabbs;
        setup.config.rayFlags = RayFlag::AcceptFirstHitAndEndSearch;
        setup.config.behavior = CommitProcedural;
        setup.config.proceduralTBase = 1.25f;
        setup.config.proceduralTPrimitiveStep = 2.0f;
        setup.expected.candidateCount = 1;
        setup.expected.proceduralCandidateCount = 1;
        expectProceduralHit(setup, 1.25f);
        break;

    case CaseID::FlagsTerminateFirstTriangles:
        setup.scene = SceneKind::TwoTriangles;
        setup.config.rayFlags = RayFlag::ForceNonOpaque | RayFlag::AcceptFirstHitAndEndSearch;
        setup.config.behavior = CommitTriangles;
        setup.expected.candidateCount = 1;
        setup.expected.triangleCandidateCount = 1;
        expectTriangleHit(setup, 1.0f);
        break;

    case CaseID::WatertightNoMissAabbs:
        setup.scene = SceneKind::SharedFaceAabbs;
        setup.config.behavior = CommitProcedural;
        setup.config.rayOriginAndTMin[0] = 0.0f;
        setup.config.rayOriginAndTMin[1] = 0.0f;
        setup.config.proceduralTBase = 1.5f;
        setup.expected.candidateCount = 1;
        setup.expected.proceduralCandidateCount = 1;
        expectProceduralHit(setup, 1.5f);
        break;

    case CaseID::WatertightSingleHitTriangles:
        setup.scene = SceneKind::SharedEdgeTriangles;
        setup.config.rayFlags = RayFlag::ForceNonOpaque;
        setup.config.behavior = CommitTriangles;
        setup.config.rayOriginAndTMin[0] = 0.0f;
        setup.config.rayOriginAndTMin[1] = 0.0f;
        setup.expected.candidateCount = 1;
        setup.expected.triangleCandidateCount = 1;
        expectTriangleHit(setup, 1.0f);
        break;

    case CaseID::InsideAabbRayEndInside:
        setup.scene = SceneKind::SingleAabb;
        setup.config.behavior = CommitProcedural;
        setup.config.rayOriginAndTMin[0] = 0.0f;
        setup.config.rayOriginAndTMin[1] = 0.0f;
        setup.config.rayOriginAndTMin[2] = 0.0f;
        setup.config.rayDirectionAndTMax[3] = 0.5f;
        setup.config.proceduralTBase = 0.25f;
        setup.expected.candidateCount = 1;
        setup.expected.proceduralCandidateCount = 1;
        expectProceduralHit(setup, 0.25f);
        break;

    case CaseID::DirectionLengthTriangles:
        setup.scene = SceneKind::DirectionTriangle;
        setup.config.rayOriginAndTMin[0] = 0.25f;
        setup.config.rayOriginAndTMin[1] = 0.25f;
        setup.config.rayOriginAndTMin[2] = 1.0f;
        setup.config.rayOriginAndTMin[3] = 3.9995f;
        setup.config.rayDirectionAndTMax[3] = 4.0005f;
        setup.expected.candidateCount = 1;
        setup.expected.triangleCandidateCount = 1;
        break;

    case CaseID::BarycentricCoordinates:
        setup.scene = SceneKind::SingleNonOpaqueTriangle;
        setup.config.behavior = CommitTriangles | CaptureBarycentrics;
        setup.expected.candidateCount = 1;
        setup.expected.triangleCandidateCount = 1;
        expectTriangleHit(setup, 1.0f);
        setup.expected.committedBarycentricU = 0.25f;
        setup.expected.committedBarycentricV = 0.25f;
        break;

    case CaseID::MultipleRayQueries:
        setup.scene = SceneKind::SingleNonOpaqueTriangle;
        setup.config.mode = Mode::MultipleQueries;
        setup.expected.auxiliary0 = CommittedStatus::CommittedTriangle;
        setup.expected.auxiliary1 = CommittedStatus::CommittedNothing;
        setup.expected.auxiliary2 = 1;
        break;

    case CaseID::StressAabbs:
        setup.scene = SceneKind::StressAabbs;
        setup.config.rayOriginAndTMin[0] = 0.0f;
        setup.config.rayOriginAndTMin[1] = 0.0f;
        setup.expected.candidateCount = 32;
        setup.expected.proceduralCandidateCount = 32;
        break;

    case CaseID::StressTriangles:
        setup.scene = SceneKind::StressTriangles;
        setup.config.rayOriginAndTMin[0] = 0.0f;
        setup.config.rayOriginAndTMin[1] = 0.0f;
        setup.expected.candidateCount = 32;
        setup.expected.triangleCandidateCount = 32;
        break;

    case CaseID::NonUniformArgsNoMiss:
        setup.scene = SceneKind::MaskedOpaqueTriangle;
        setup.config.instanceMask = 0x0f;
        expectTriangleHit(setup, 1.0f);
        break;

    case CaseID::NonUniformArgsCullMaskMiss:
        setup.scene = SceneKind::MaskedOpaqueTriangle;
        setup.config.instanceMask = 0xf0;
        break;

    case CaseID::BuiltinGeometryIndexCandidate:
        setup.scene = SceneKind::GeometryIndex;
        setup.config.behavior = CommitTriangles | CaptureFirstCandidateMetadata | CaptureCommittedMetadata;
        setup.expected.candidateCount = 1;
        setup.expected.triangleCandidateCount = 1;
        setup.expected.firstCandidatePrimitiveIndex = 0;
        setup.expected.firstCandidateGeometryIndex = 1;
        setup.expected.firstCandidateInstanceID = 17;
        setup.expected.firstCandidateInstanceIndex = 0;
        expectTriangleHit(setup, 2.0f);
        setup.expected.committedPrimitiveIndex = 0;
        setup.expected.committedGeometryIndex = 1;
        setup.expected.committedInstanceID = 17;
        setup.expected.committedInstanceIndex = 0;
        break;

    case CaseID::BuiltinPrimitiveIDAabbs:
        setup.scene = SceneKind::PrimitiveIDAabbs;
        setup.config.behavior = CommitProcedural | CaptureFirstCandidateMetadata | CaptureCommittedMetadata;
        setup.config.proceduralTBase = 1.5f;
        setup.config.proceduralTPrimitiveStep = 1.0f;
        setup.expected.candidateCount = 1;
        setup.expected.proceduralCandidateCount = 1;
        setup.expected.firstCandidatePrimitiveIndex = 2;
        setup.expected.firstCandidateGeometryIndex = 0;
        setup.expected.firstCandidateInstanceID = 17;
        setup.expected.firstCandidateInstanceIndex = 0;
        expectProceduralHit(setup, 3.5f);
        setup.expected.committedPrimitiveIndex = 2;
        setup.expected.committedGeometryIndex = 0;
        setup.expected.committedInstanceID = 17;
        setup.expected.committedInstanceIndex = 0;
        break;

    case CaseID::BuiltinInstanceCustomIndex:
        setup.scene = SceneKind::InstanceCustomIndex;
        setup.config.behavior = CommitTriangles | CaptureFirstCandidateMetadata | CaptureCommittedMetadata;
        setup.expected.candidateCount = 1;
        setup.expected.triangleCandidateCount = 1;
        setup.expected.firstCandidatePrimitiveIndex = 0;
        setup.expected.firstCandidateGeometryIndex = 0;
        setup.expected.firstCandidateInstanceID = 37;
        setup.expected.firstCandidateInstanceIndex = 1;
        expectTriangleHit(setup, 1.0f);
        setup.expected.committedPrimitiveIndex = 0;
        setup.expected.committedGeometryIndex = 0;
        setup.expected.committedInstanceID = 37;
        setup.expected.committedInstanceIndex = 1;
        break;

    case CaseID::BuiltinObjectToWorld:
        setup.scene = SceneKind::ObjectToWorld;
        setup.config.behavior = CommitTriangles | CaptureTransform;
        setup.config.rayOriginAndTMin[0] = 0.5f;
        setup.config.rayOriginAndTMin[1] = 0.125f;
        setup.expected.candidateCount = 1;
        setup.expected.triangleCandidateCount = 1;
        expectTriangleHit(setup, 4.0f);
        setup.expected.transformM00 = -2.0f;
        setup.expected.transformM03 = 1.0f;
        setup.expected.transformM11 = 0.5f;
        setup.expected.transformM23 = 3.0f;
        break;

    case CaseID::Count:
        FAIL("CaseID::Count is not a test case");
        break;
    }
    return setup;
}

class RayQueryScene
{
public:
    RayQueryScene(IDevice* device, ICommandQueue* queue)
        : m_device(device)
        , m_queue(queue)
    {
    }

    IAccelerationStructure* build(SceneKind sceneKind)
    {
        switch (sceneKind)
        {
        case SceneKind::SingleOpaqueTriangle:
        {
            auto bottomLevel = buildTriangles({{makeTriangle(1.0f), true}});
            buildTopLevel({{bottomLevel}});
            break;
        }
        case SceneKind::MaskedOpaqueTriangle:
        {
            auto bottomLevel = buildTriangles({{makeTriangle(1.0f), true}});
            InstanceSpec instance = {bottomLevel};
            instance.instanceMask = 0x0f;
            buildTopLevel({instance});
            break;
        }
        case SceneKind::SingleNonOpaqueTriangle:
        {
            auto bottomLevel = buildTriangles({{makeTriangle(1.0f), false}});
            buildTopLevel({{bottomLevel}});
            break;
        }
        case SceneKind::FrontFacingOpaqueTriangle:
        {
            auto bottomLevel = buildTriangles({{makeTriangle(1.0f), true}});
            InstanceSpec instance = {bottomLevel};
            instance.flags = AccelerationStructureInstanceFlags::TriangleFrontCounterClockwise;
            buildTopLevel({instance});
            break;
        }
        case SceneKind::SingleAabb:
        {
            auto bottomLevel = buildAabbs({{{makeAabb(-1.0f, 1.0f, 0.0f, 1.0f)}, false}});
            buildTopLevel({{bottomLevel}});
            break;
        }
        case SceneKind::ProceduralWall:
        {
            AabbGeometry geometry;
            geometry.bounds.resize(4, makeAabb(-1.0f, 1.0f, 0.5f, 1.0f));
            auto bottomLevel = buildAabbs({geometry});
            buildTopLevel({{bottomLevel}});
            break;
        }
        case SceneKind::TriangleInBetween:
        {
            auto procedural = buildAabbs({{{makeAabb(-1.0f, 1.0f, 0.5f, 1.0f)}, false}});
            auto triangle = buildTriangles({{makeTriangle(2.0f), true}});
            InstanceSpec proceduralInstance = {procedural};
            proceduralInstance.instanceID = 19;
            InstanceSpec triangleInstance = {triangle};
            triangleInstance.instanceID = 23;
            buildTopLevel({proceduralInstance, triangleInstance});
            break;
        }
        case SceneKind::TwoAabbs:
        {
            AabbGeometry geometry;
            geometry.bounds = {
                makeAabb(-1.0f, 1.0f, 0.5f, 1.0f),
                makeAabb(-1.0f, 1.0f, 2.5f, 3.0f),
            };
            auto bottomLevel = buildAabbs({geometry});
            buildTopLevel({{bottomLevel}});
            break;
        }
        case SceneKind::TwoTriangles:
        {
            TriangleGeometry geometry;
            geometry.vertices = makeTriangle(1.0f);
            const auto second = makeTriangle(2.0f);
            geometry.vertices.insert(geometry.vertices.end(), second.begin(), second.end());
            auto bottomLevel = buildTriangles({geometry});
            buildTopLevel({{bottomLevel}});
            break;
        }
        case SceneKind::SharedFaceAabbs:
        {
            AabbGeometry geometry;
            geometry.bounds = {
                {-1.0f, -1.0f, 1.0f, 0.0f, 1.0f, 2.0f},
                {0.0f, -1.0f, 1.0f, 1.0f, 1.0f, 2.0f},
            };
            auto bottomLevel = buildAabbs({geometry});
            buildTopLevel({{bottomLevel}});
            break;
        }
        case SceneKind::SharedEdgeTriangles:
        {
            TriangleGeometry geometry;
            geometry.vertices = {
                {-1.0f, -1.0f, 1.0f},
                {1.0f, -1.0f, 1.0f},
                {-1.0f, 1.0f, 1.0f},
                {1.0f, -1.0f, 1.0f},
                {1.0f, 1.0f, 1.0f},
                {-1.0f, 1.0f, 1.0f},
            };
            auto bottomLevel = buildTriangles({geometry});
            buildTopLevel({{bottomLevel}});
            break;
        }
        case SceneKind::DirectionTriangle:
        {
            auto bottomLevel = buildTriangles({{makeTriangle(5.0f), false}});
            buildTopLevel({{bottomLevel}});
            break;
        }
        case SceneKind::StressAabbs:
        {
            AabbGeometry geometry;
            for (uint32_t i = 0; i < 32; ++i)
            {
                const float z = 1.0f + float(i);
                geometry.bounds.push_back(makeAabb(-0.5f, 0.5f, z, z + 0.25f));
            }
            auto bottomLevel = buildAabbs({geometry});
            buildTopLevel({{bottomLevel}});
            break;
        }
        case SceneKind::StressTriangles:
        {
            TriangleGeometry geometry;
            for (uint32_t i = 0; i < 32; ++i)
            {
                const auto triangle = makeCenteredTriangle(1.0f + float(i));
                geometry.vertices.insert(geometry.vertices.end(), triangle.begin(), triangle.end());
            }
            auto bottomLevel = buildTriangles({geometry});
            buildTopLevel({{bottomLevel}});
            break;
        }
        case SceneKind::GeometryIndex:
        {
            const TriangleGeometry missedGeometry = {makeTranslatedTriangle(1.0f, 10.0f), false};
            const TriangleGeometry hitGeometry = {makeTriangle(2.0f), false};
            auto bottomLevel = buildTriangles({missedGeometry, hitGeometry});
            buildTopLevel({{bottomLevel}});
            break;
        }
        case SceneKind::PrimitiveIDAabbs:
        {
            AabbGeometry geometry;
            geometry.bounds = {
                makeAabb(5.0f, 6.0f, 1.0f, 2.0f),
                makeAabb(-6.0f, -5.0f, 1.0f, 2.0f),
                makeAabb(-1.0f, 1.0f, 1.0f, 2.0f),
            };
            auto bottomLevel = buildAabbs({geometry});
            buildTopLevel({{bottomLevel}});
            break;
        }
        case SceneKind::InstanceCustomIndex:
        {
            auto bottomLevel = buildTriangles({{makeTriangle(1.0f), false}});
            InstanceSpec missedInstance = {bottomLevel};
            missedInstance.transform[3] = 10.0f;
            missedInstance.instanceID = 11;
            InstanceSpec hitInstance = {bottomLevel};
            hitInstance.instanceID = 37;
            buildTopLevel({missedInstance, hitInstance});
            break;
        }
        case SceneKind::ObjectToWorld:
        {
            auto bottomLevel = buildTriangles({{makeTriangle(1.0f), false}});
            InstanceSpec instance = {bottomLevel};
            instance.transform = {
                -2.0f,
                0.0f,
                0.0f,
                1.0f,
                0.0f,
                0.5f,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                1.0f,
                3.0f,
            };
            instance.instanceID = 51;
            buildTopLevel({instance});
            break;
        }
        }
        return m_topLevel;
    }

private:
    static std::vector<std::array<float, 3>> makeTriangle(float z)
    {
        return {
            {0.0f, 0.0f, z},
            {1.0f, 0.0f, z},
            {0.0f, 1.0f, z},
        };
    }

    static std::vector<std::array<float, 3>> makeCenteredTriangle(float z)
    {
        return {
            {-1.0f, -1.0f, z},
            {1.0f, -1.0f, z},
            {0.0f, 1.0f, z},
        };
    }

    static std::vector<std::array<float, 3>> makeTranslatedTriangle(float z, float x)
    {
        auto triangle = makeTriangle(z);
        for (auto& vertex : triangle)
            vertex[0] += x;
        return triangle;
    }

    static AccelerationStructureAABB makeAabb(float minXY, float maxXY, float minZ, float maxZ)
    {
        return {minXY, minXY, minZ, maxXY, maxXY, maxZ};
    }

    ComPtr<IBuffer> createBuildBuffer(const void* data, size_t size)
    {
        BufferDesc desc = {};
        desc.size = size;
        desc.usage = BufferUsage::AccelerationStructureBuildInput;
        desc.defaultState = ResourceState::AccelerationStructureBuildInput;
        ComPtr<IBuffer> buffer;
        REQUIRE_CALL(m_device->createBuffer(desc, data, buffer.writeRef()));
        m_buffers.push_back(buffer);
        return buffer;
    }

    ComPtr<IAccelerationStructure> buildAccelerationStructure(
        const AccelerationStructureBuildDesc& buildDesc,
        AccelerationStructureKind kind
    )
    {
        AccelerationStructureSizes sizes = {};
        REQUIRE_CALL(m_device->getAccelerationStructureSizes(buildDesc, &sizes));

        BufferDesc scratchDesc = {};
        scratchDesc.size = sizes.scratchSize;
        scratchDesc.usage = BufferUsage::UnorderedAccess;
        scratchDesc.defaultState = ResourceState::UnorderedAccess;
        ComPtr<IBuffer> scratch;
        REQUIRE_CALL(m_device->createBuffer(scratchDesc, nullptr, scratch.writeRef()));

        AccelerationStructureDesc desc = {};
        desc.kind = kind;
        desc.size = sizes.accelerationStructureSize;
        ComPtr<IAccelerationStructure> accelerationStructure;
        REQUIRE_CALL(m_device->createAccelerationStructure(desc, accelerationStructure.writeRef()));

        ComPtr<ICommandEncoder> encoder = m_queue->createCommandEncoder();
        encoder->buildAccelerationStructure(buildDesc, accelerationStructure, nullptr, scratch, 0, nullptr);
        REQUIRE_CALL(m_queue->submit(encoder->finish()));
        REQUIRE_CALL(m_queue->waitOnHost());
        return accelerationStructure;
    }

    IAccelerationStructure* buildTriangles(const std::vector<TriangleGeometry>& geometries)
    {
        std::vector<AccelerationStructureBuildInput> inputs;
        inputs.reserve(geometries.size());
        for (const TriangleGeometry& geometry : geometries)
        {
            ComPtr<IBuffer> vertexBuffer =
                createBuildBuffer(geometry.vertices.data(), geometry.vertices.size() * sizeof(geometry.vertices[0]));

            AccelerationStructureBuildInput input = {};
            input.type = AccelerationStructureBuildInputType::Triangles;
            input.triangles.vertexBuffers[0] = vertexBuffer;
            input.triangles.vertexBufferCount = 1;
            input.triangles.vertexFormat = Format::RGB32Float;
            input.triangles.vertexCount = uint32_t(geometry.vertices.size());
            input.triangles.vertexStride = sizeof(geometry.vertices[0]);
            input.triangles.flags =
                geometry.opaque ? AccelerationStructureGeometryFlags::Opaque : AccelerationStructureGeometryFlags::None;
            inputs.push_back(input);
        }

        AccelerationStructureBuildDesc buildDesc = {};
        buildDesc.inputs = inputs.data();
        buildDesc.inputCount = uint32_t(inputs.size());
        ComPtr<IAccelerationStructure> bottomLevel =
            buildAccelerationStructure(buildDesc, AccelerationStructureKind::BottomLevel);
        m_bottomLevels.push_back(bottomLevel);
        return bottomLevel;
    }

    IAccelerationStructure* buildAabbs(const std::vector<AabbGeometry>& geometries)
    {
        std::vector<AccelerationStructureBuildInput> inputs;
        inputs.reserve(geometries.size());
        for (const AabbGeometry& geometry : geometries)
        {
            ComPtr<IBuffer> aabbBuffer =
                createBuildBuffer(geometry.bounds.data(), geometry.bounds.size() * sizeof(geometry.bounds[0]));

            AccelerationStructureBuildInput input = {};
            input.type = AccelerationStructureBuildInputType::ProceduralPrimitives;
            input.proceduralPrimitives.aabbBuffers[0] = aabbBuffer;
            input.proceduralPrimitives.aabbBufferCount = 1;
            input.proceduralPrimitives.aabbStride = sizeof(geometry.bounds[0]);
            input.proceduralPrimitives.primitiveCount = uint32_t(geometry.bounds.size());
            input.proceduralPrimitives.flags =
                geometry.opaque ? AccelerationStructureGeometryFlags::Opaque : AccelerationStructureGeometryFlags::None;
            inputs.push_back(input);
        }

        AccelerationStructureBuildDesc buildDesc = {};
        buildDesc.inputs = inputs.data();
        buildDesc.inputCount = uint32_t(inputs.size());
        ComPtr<IAccelerationStructure> bottomLevel =
            buildAccelerationStructure(buildDesc, AccelerationStructureKind::BottomLevel);
        m_bottomLevels.push_back(bottomLevel);
        return bottomLevel;
    }

    void buildTopLevel(const std::vector<InstanceSpec>& instances)
    {
        std::vector<AccelerationStructureInstanceDescGeneric> genericDescs(instances.size());
        for (size_t i = 0; i < instances.size(); ++i)
        {
            const InstanceSpec& instance = instances[i];
            AccelerationStructureInstanceDescGeneric& desc = genericDescs[i];
            std::memcpy(desc.transform, instance.transform.data(), sizeof(desc.transform));
            desc.instanceID = instance.instanceID;
            desc.instanceMask = instance.instanceMask;
            desc.instanceContributionToHitGroupIndex = instance.contribution;
            desc.flags = instance.flags;
            desc.accelerationStructure = instance.bottomLevel->getHandle();
        }

        const AccelerationStructureInstanceDescType nativeType = getAccelerationStructureInstanceDescType(m_device);
        const Size nativeStride = getAccelerationStructureInstanceDescSize(nativeType);
        std::vector<uint8_t> nativeDescs(genericDescs.size() * nativeStride);
        convertAccelerationStructureInstanceDescs(
            genericDescs.size(),
            nativeType,
            nativeDescs.data(),
            nativeStride,
            genericDescs.data(),
            sizeof(genericDescs[0])
        );

        BufferDesc instanceBufferDesc = {};
        instanceBufferDesc.size = nativeDescs.size();
        instanceBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        instanceBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        ComPtr<IBuffer> instanceBuffer;
        REQUIRE_CALL(m_device->createBuffer(instanceBufferDesc, nativeDescs.data(), instanceBuffer.writeRef()));
        m_buffers.push_back(instanceBuffer);

        AccelerationStructureBuildInput input = {};
        input.type = AccelerationStructureBuildInputType::Instances;
        input.instances.instanceBuffer = instanceBuffer;
        input.instances.instanceStride = uint32_t(nativeStride);
        input.instances.instanceCount = uint32_t(instances.size());

        AccelerationStructureBuildDesc buildDesc = {};
        buildDesc.inputs = &input;
        buildDesc.inputCount = 1;
        m_topLevel = buildAccelerationStructure(buildDesc, AccelerationStructureKind::TopLevel);
    }

    IDevice* m_device = nullptr;
    ICommandQueue* m_queue = nullptr;
    std::vector<ComPtr<IBuffer>> m_buffers;
    std::vector<ComPtr<IAccelerationStructure>> m_bottomLevels;
    ComPtr<IAccelerationStructure> m_topLevel;
};

bool floatEqual(float actual, float expected)
{
    return std::abs(actual - expected) <= 0.00001f;
}

void checkFloatEqual(float actual, float expected)
{
    CAPTURE(actual);
    CAPTURE(expected);
    CHECK(floatEqual(actual, expected));
}

void checkExactResult(const TestResult& actual, const TestResult& expected)
{
    CHECK_EQ(actual.committedStatus, expected.committedStatus);
    CHECK_EQ(actual.candidateCount, expected.candidateCount);
    CHECK_EQ(actual.triangleCandidateCount, expected.triangleCandidateCount);
    CHECK_EQ(actual.proceduralCandidateCount, expected.proceduralCandidateCount);
    CHECK_EQ(actual.firstCandidatePrimitiveIndex, expected.firstCandidatePrimitiveIndex);
    CHECK_EQ(actual.firstCandidateGeometryIndex, expected.firstCandidateGeometryIndex);
    CHECK_EQ(actual.firstCandidateInstanceID, expected.firstCandidateInstanceID);
    CHECK_EQ(actual.firstCandidateInstanceIndex, expected.firstCandidateInstanceIndex);
    CHECK_EQ(actual.committedPrimitiveIndex, expected.committedPrimitiveIndex);
    CHECK_EQ(actual.committedGeometryIndex, expected.committedGeometryIndex);
    CHECK_EQ(actual.committedInstanceID, expected.committedInstanceID);
    CHECK_EQ(actual.committedInstanceIndex, expected.committedInstanceIndex);
    checkFloatEqual(actual.committedT, expected.committedT);
    CHECK_EQ(actual.committedFrontFace, expected.committedFrontFace);
    checkFloatEqual(actual.committedBarycentricU, expected.committedBarycentricU);
    checkFloatEqual(actual.committedBarycentricV, expected.committedBarycentricV);
    checkFloatEqual(actual.transformM00, expected.transformM00);
    checkFloatEqual(actual.transformM03, expected.transformM03);
    checkFloatEqual(actual.transformM11, expected.transformM11);
    checkFloatEqual(actual.transformM23, expected.transformM23);
    CHECK_EQ(actual.auxiliary0, expected.auxiliary0);
    CHECK_EQ(actual.auxiliary1, expected.auxiliary1);
    CHECK_EQ(actual.auxiliary2, expected.auxiliary2);
    CHECK_EQ(actual.auxiliary3, expected.auxiliary3);
}

void checkResult(CaseID caseID, const TestResult& actual, const TestResult& expected)
{
    if (caseID == CaseID::FlagsTerminateFirstAabbs || caseID == CaseID::FlagsTerminateFirstTriangles)
    {
        const bool isProcedural = caseID == CaseID::FlagsTerminateFirstAabbs;
        const float farT = isProcedural ? 3.25f : 2.0f;
        const bool matchesNear = floatEqual(actual.committedT, expected.committedT);
        const bool matchesFar = floatEqual(actual.committedT, farT);
        const bool matchesAllowedHit = matchesNear || matchesFar;
        CAPTURE(actual.committedT);
        CHECK(matchesAllowedHit);

        // The source CTS only checks that this flag produces a valid hit; it calls Proceed once
        // and does not constrain the total number of shader-managed candidates. D3D12 WARP may
        // expose both candidates before completing the query. Keep this adapted CTS oracle at the
        // same level, while the CPU state-machine tests separately require accept-first commits to
        // make the next Proceed call return false.
        const bool hasAllowedCandidateCount = actual.candidateCount == 1 || actual.candidateCount == 2;
        CAPTURE(actual.candidateCount);
        CHECK(hasAllowedCandidateCount);
        if (isProcedural)
            CHECK_EQ(actual.proceduralCandidateCount, actual.candidateCount);
        else
            CHECK_EQ(actual.triangleCandidateCount, actual.candidateCount);

        TestResult normalized = actual;
        normalized.committedT = expected.committedT;
        normalized.candidateCount = expected.candidateCount;
        normalized.proceduralCandidateCount = expected.proceduralCandidateCount;
        normalized.triangleCandidateCount = expected.triangleCandidateCount;
        checkExactResult(normalized, expected);
        return;
    }
    if (caseID == CaseID::WatertightNoMissAabbs)
    {
        const bool hasOneOrTwoSharedFaceHits = actual.candidateCount == 1 || actual.candidateCount == 2;
        CAPTURE(actual.candidateCount);
        CHECK(hasOneOrTwoSharedFaceHits);
        CHECK_EQ(actual.proceduralCandidateCount, actual.candidateCount);
        TestResult normalized = actual;
        normalized.candidateCount = expected.candidateCount;
        normalized.proceduralCandidateCount = expected.proceduralCandidateCount;
        checkExactResult(normalized, expected);
        return;
    }
    checkExactResult(actual, expected);
}

void runRayQueryCtsCase(IDevice* device, CaseID caseID)
{
    REQUIRE(device->hasFeature(Feature::RayQuery));

    CaseSetup setup = getCaseSetup(caseID);
    ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
    REQUIRE(queue);

    RayQueryScene scene(device, queue);
    IAccelerationStructure* topLevel = scene.build(setup.scene);
    REQUIRE(topLevel);

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadAndLinkProgram(device, "test-ray-query-cts", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram;
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    BufferDesc configDesc = {};
    configDesc.size = sizeof(setup.config);
    configDesc.elementSize = sizeof(setup.config);
    configDesc.usage = BufferUsage::ShaderResource;
    configDesc.defaultState = ResourceState::ShaderResource;
    ComPtr<IBuffer> configBuffer;
    REQUIRE_CALL(device->createBuffer(configDesc, &setup.config, configBuffer.writeRef()));

    BufferDesc resultDesc = {};
    resultDesc.size = sizeof(TestResult);
    resultDesc.elementSize = sizeof(TestResult);
    resultDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    resultDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> resultBuffer;
    REQUIRE_CALL(device->createBuffer(resultDesc, nullptr, resultBuffer.writeRef()));

    ComPtr<ICommandEncoder> commandEncoder = queue->createCommandEncoder();
    auto rootObject = device->createRootShaderObject(pipeline);
    ShaderCursor cursor(rootObject->getEntryPoint(0));
    cursor["scene"].setBinding(topLevel);
    cursor["configBuffer"].setBinding(configBuffer);
    cursor["resultBuffer"].setBinding(resultBuffer);

    auto pass = commandEncoder->beginComputePass();
    pass->bindPipeline(pipeline, rootObject);
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    REQUIRE_CALL(queue->submit(commandEncoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());

    TestResult actual = {};
    REQUIRE_CALL(device->readBuffer(resultBuffer, 0, sizeof(actual), &actual));
    checkResult(caseID, actual, setup.expected);
}

} // namespace

#ifdef SLANG_RHI_ENABLE_CPU_RAY_QUERY
#define RAY_QUERY_CTS_DEVICE_TYPES CPU | D3D12
#else
#define RAY_QUERY_CTS_DEVICE_TYPES D3D12
#endif

#define RAY_QUERY_CTS_CASE(name, caseID)                                                                               \
    GPU_TEST_CASE(name, RAY_QUERY_CTS_DEVICE_TYPES)                                                                    \
    {                                                                                                                  \
        SLANG_UNUSED(ctx);                                                                                             \
        runRayQueryCtsCase(device, CaseID::caseID);                                                                    \
    }

RAY_QUERY_CTS_CASE("ray-query-cts-procedural-object-behind-bounding-boxes", ProceduralObjectBehindBoundingBoxes)
RAY_QUERY_CTS_CASE("ray-query-cts-procedural-triangle-in-between", ProceduralTriangleInBetween)
RAY_QUERY_CTS_CASE("ray-query-cts-traversal-generate-aabbs", TraversalGenerateAabbs)
RAY_QUERY_CTS_CASE("ray-query-cts-traversal-generate-triangles", TraversalGenerateTriangles)
RAY_QUERY_CTS_CASE("ray-query-cts-traversal-skip-aabbs", TraversalSkipAabbs)
RAY_QUERY_CTS_CASE("ray-query-cts-traversal-skip-triangles", TraversalSkipTriangles)
RAY_QUERY_CTS_CASE("ray-query-cts-flags-cull-back-facing-triangles", FlagsCullBackFacingTriangles)
RAY_QUERY_CTS_CASE("ray-query-cts-flags-cull-front-facing-triangles", FlagsCullFrontFacingTriangles)
RAY_QUERY_CTS_CASE("ray-query-cts-flags-force-non-opaque", FlagsForceNonOpaque)
RAY_QUERY_CTS_CASE("ray-query-cts-builtin-ray-query-terminate", BuiltinRayQueryTerminate)
RAY_QUERY_CTS_CASE("ray-query-cts-flags-cull-non-opaque", FlagsCullNonOpaque)
RAY_QUERY_CTS_CASE("ray-query-cts-flags-cull-opaque", FlagsCullOpaque)
RAY_QUERY_CTS_CASE("ray-query-cts-flags-skip-aabbs", FlagsSkipAabbs)
RAY_QUERY_CTS_CASE("ray-query-cts-flags-skip-triangles", FlagsSkipTriangles)
RAY_QUERY_CTS_CASE("ray-query-cts-flags-terminate-first-aabbs", FlagsTerminateFirstAabbs)
RAY_QUERY_CTS_CASE("ray-query-cts-flags-terminate-first-triangles", FlagsTerminateFirstTriangles)
RAY_QUERY_CTS_CASE("ray-query-cts-watertight-no-miss-aabbs", WatertightNoMissAabbs)
RAY_QUERY_CTS_CASE("ray-query-cts-watertight-single-hit-triangles", WatertightSingleHitTriangles)
RAY_QUERY_CTS_CASE("ray-query-cts-inside-aabb-ray-end-inside", InsideAabbRayEndInside)
RAY_QUERY_CTS_CASE("ray-query-cts-direction-length-triangles", DirectionLengthTriangles)
RAY_QUERY_CTS_CASE("ray-query-cts-barycentric-coordinates", BarycentricCoordinates)
RAY_QUERY_CTS_CASE("ray-query-cts-multiple-ray-queries", MultipleRayQueries)
RAY_QUERY_CTS_CASE("ray-query-cts-stress-aabbs", StressAabbs)
RAY_QUERY_CTS_CASE("ray-query-cts-stress-triangles", StressTriangles)
RAY_QUERY_CTS_CASE("ray-query-cts-non-uniform-args-no-miss", NonUniformArgsNoMiss)
RAY_QUERY_CTS_CASE("ray-query-cts-non-uniform-args-cull-mask-miss", NonUniformArgsCullMaskMiss)
RAY_QUERY_CTS_CASE("ray-query-cts-builtin-geometry-index-candidate", BuiltinGeometryIndexCandidate)
RAY_QUERY_CTS_CASE("ray-query-cts-builtin-primitive-id-aabbs", BuiltinPrimitiveIDAabbs)
RAY_QUERY_CTS_CASE("ray-query-cts-builtin-instance-custom-index", BuiltinInstanceCustomIndex)
RAY_QUERY_CTS_CASE("ray-query-cts-builtin-object-to-world", BuiltinObjectToWorld)

#undef RAY_QUERY_CTS_CASE
#undef RAY_QUERY_CTS_DEVICE_TYPES
