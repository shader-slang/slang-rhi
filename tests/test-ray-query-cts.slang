// SPDX-FileCopyrightText: 2015, 2020-2021 The Khronos Group Inc.
// SPDX-FileCopyrightText: 2020, 2023 Advanced Micro Devices, Inc.
// SPDX-FileCopyrightText: 2021 Valve Corporation
// SPDX-License-Identifier: Apache-2.0
//
// These tests are semantic adaptations of selected VK-GL-CTS ray-query cases. They use
// Slang/DXR inline-ray-query operations so that the same shader runs through slang-rhi's CPU and
// D3D12 backends.

struct CaseConfig
{
    uint mode;
    uint rayFlags;
    uint instanceMask;
    uint behavior;
    float4 rayOriginAndTMin;
    float4 rayDirectionAndTMax;
    float proceduralTBase;
    float proceduralTPrimitiveStep;
    float proceduralTGeometryStep;
    uint reserved;
};

struct TestResult
{
    uint committedStatus;
    uint candidateCount;
    uint triangleCandidateCount;
    uint proceduralCandidateCount;

    uint firstCandidatePrimitiveIndex;
    uint firstCandidateGeometryIndex;
    uint firstCandidateInstanceID;
    uint firstCandidateInstanceIndex;

    uint committedPrimitiveIndex;
    uint committedGeometryIndex;
    uint committedInstanceID;
    uint committedInstanceIndex;

    float committedT;
    uint committedFrontFace;
    float committedBarycentricU;
    float committedBarycentricV;

    float transformM00;
    float transformM03;
    float transformM11;
    float transformM23;

    uint auxiliary0;
    uint auxiliary1;
    uint auxiliary2;
    uint auxiliary3;
};

static const uint kModeStandard = 0;
static const uint kModeMultipleQueries = 1;

static const uint kCommitTriangles = 0x01;
static const uint kCommitProcedural = 0x02;
static const uint kAbortAfterCandidate = 0x04;
static const uint kCaptureFirstCandidateMetadata = 0x08;
static const uint kCaptureCommittedMetadata = 0x10;
static const uint kCaptureBarycentrics = 0x20;
static const uint kCaptureTransform = 0x40;

TestResult makeDefaultResult()
{
    TestResult result = {};
    result.firstCandidatePrimitiveIndex = 0xffffffff;
    result.firstCandidateGeometryIndex = 0xffffffff;
    result.firstCandidateInstanceID = 0xffffffff;
    result.firstCandidateInstanceIndex = 0xffffffff;
    result.committedPrimitiveIndex = 0xffffffff;
    result.committedGeometryIndex = 0xffffffff;
    result.committedInstanceID = 0xffffffff;
    result.committedInstanceIndex = 0xffffffff;
    result.committedT = -1.0;
    return result;
}

void captureFirstCandidateMetadata(inout TestResult result, RayQuery<0> query)
{
    if (result.candidateCount != 1)
        return;

    result.firstCandidatePrimitiveIndex = query.CandidatePrimitiveIndex();
    result.firstCandidateGeometryIndex = query.CandidateGeometryIndex();
    result.firstCandidateInstanceID = query.CandidateInstanceID();
    result.firstCandidateInstanceIndex = query.CandidateInstanceIndex();
}

void captureTransform(inout TestResult result, RayQuery<0> query)
{
    float3x4 transform = query.CandidateObjectToWorld3x4();
    result.transformM00 = transform[0][0];
    result.transformM03 = transform[0][3];
    result.transformM11 = transform[1][1];
    result.transformM23 = transform[2][3];
}

void finishQuery(inout TestResult result, RayQuery<0> query, uint behavior)
{
    result.committedStatus = query.CommittedStatus();
    if (result.committedStatus == COMMITTED_NOTHING)
        return;

    result.committedT = query.CommittedRayT();
    if ((behavior & kCaptureCommittedMetadata) != 0)
    {
        result.committedPrimitiveIndex = query.CommittedPrimitiveIndex();
        result.committedGeometryIndex = query.CommittedGeometryIndex();
        result.committedInstanceID = query.CommittedInstanceID();
        result.committedInstanceIndex = query.CommittedInstanceIndex();
    }
    if ((behavior & kCaptureBarycentrics) != 0 &&
        result.committedStatus == COMMITTED_TRIANGLE_HIT)
    {
        result.committedFrontFace = query.CommittedTriangleFrontFace() ? 1 : 0;
        float2 barycentrics = query.CommittedTriangleBarycentrics();
        result.committedBarycentricU = barycentrics.x;
        result.committedBarycentricV = barycentrics.y;
    }
}

TestResult runStandardQuery(
    RaytracingAccelerationStructure scene,
    CaseConfig config)
{
    TestResult result = makeDefaultResult();
    RayDesc ray = {
        config.rayOriginAndTMin.xyz,
        config.rayOriginAndTMin.w,
        config.rayDirectionAndTMax.xyz,
        config.rayDirectionAndTMax.w,
    };

    RayQuery<0> query;
    query.TraceRayInline(scene, config.rayFlags, config.instanceMask, ray);
    while (query.Proceed())
    {
        ++result.candidateCount;
        if ((config.behavior & kCaptureFirstCandidateMetadata) != 0)
            captureFirstCandidateMetadata(result, query);

        if (query.CandidateType() == CANDIDATE_PROCEDURAL_PRIMITIVE)
        {
            ++result.proceduralCandidateCount;
            if ((config.behavior & kCommitProcedural) != 0)
            {
                float hitT =
                    config.proceduralTBase +
                    float(query.CandidatePrimitiveIndex()) * config.proceduralTPrimitiveStep +
                    float(query.CandidateGeometryIndex()) * config.proceduralTGeometryStep;
                if (hitT >= query.RayTMin() && hitT <= query.CommittedRayT())
                    query.CommitProceduralPrimitiveHit(hitT);
            }
        }
        else if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            ++result.triangleCandidateCount;
            if ((config.behavior & kCaptureTransform) != 0 && result.triangleCandidateCount == 1)
                captureTransform(result, query);
            if ((config.behavior & kCommitTriangles) != 0)
            {
                query.CommitNonOpaqueTriangleHit();
            }
        }

        if ((config.behavior & kAbortAfterCandidate) != 0)
            query.Abort();
    }

    finishQuery(result, query, config.behavior);
    return result;
}

TestResult runMultipleQueries(
    RaytracingAccelerationStructure scene,
    CaseConfig config)
{
    TestResult result = makeDefaultResult();
    RayDesc hitRay = {
        config.rayOriginAndTMin.xyz,
        config.rayOriginAndTMin.w,
        config.rayDirectionAndTMax.xyz,
        config.rayDirectionAndTMax.w,
    };
    RayDesc missRay = hitRay;
    missRay.Origin.xy = float2(2.0, 2.0);

    RayQuery<0> hitQuery;
    RayQuery<0> missQuery;
    hitQuery.TraceRayInline(scene, config.rayFlags, config.instanceMask, hitRay);
    missQuery.TraceRayInline(scene, config.rayFlags, config.instanceMask, missRay);

    bool hitActive = true;
    bool missActive = true;
    uint hitCandidateCount = 0;
    uint missCandidateCount = 0;
    while (hitActive || missActive)
    {
        if (hitActive)
        {
            if (hitQuery.Proceed())
            {
                ++hitCandidateCount;
                if (hitQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
                    hitQuery.CommitNonOpaqueTriangleHit();
            }
            else
            {
                hitActive = false;
            }
        }

        if (missActive)
        {
            if (missQuery.Proceed())
            {
                ++missCandidateCount;
                if (missQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
                    missQuery.CommitNonOpaqueTriangleHit();
            }
            else
            {
                missActive = false;
            }
        }
    }

    result.auxiliary0 = hitQuery.CommittedStatus();
    result.auxiliary1 = missQuery.CommittedStatus();
    result.auxiliary2 = hitCandidateCount;
    result.auxiliary3 = missCandidateCount;
    return result;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(
    uniform RaytracingAccelerationStructure scene,
    uniform StructuredBuffer<CaseConfig> configBuffer,
    uniform RWStructuredBuffer<TestResult> resultBuffer)
{
    CaseConfig config = configBuffer[0];
    if (config.mode == kModeMultipleQueries)
        resultBuffer[0] = runMultipleQueries(scene, config);
    else
        resultBuffer[0] = runStandardQuery(scene, config);
}
