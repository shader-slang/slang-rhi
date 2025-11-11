#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

// List of retained blobs to prevent them from being released too early.
static std::vector<ComPtr<ISlangBlob>> s_blobs;

inline bool isEqual(const CompilationReport* a, const CompilationReport* b)
{
    return std::memcmp(a, b, offsetof(CompilationReport, entryPointReports)) == 0 &&
           a->entryPointReportCount == b->entryPointReportCount && a->pipelineReportCount == b->pipelineReportCount &&
           std::memcmp(
               a->entryPointReports,
               b->entryPointReports,
               a->entryPointReportCount * sizeof(CompilationReport::EntryPointReport)
           ) == 0 &&
           std::memcmp(
               a->pipelineReports,
               b->pipelineReports,
               a->pipelineReportCount * sizeof(CompilationReport::PipelineReport)
           ) == 0;
}

inline ComPtr<IShaderProgram> createShaderProgram(IDevice* device)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadComputeProgramFromSource(
        device,
        R"(
    [shader("compute")]
    [numthreads(1, 1, 1)]
    void computeMain() {}
    )",
        shaderProgram.writeRef()
    ));
    return shaderProgram;
}

inline ComPtr<IComputePipeline> createPipeline(IDevice* device, IShaderProgram* shaderProgram)
{
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram;
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
    return pipeline;
}

inline void dispatchPipeline(IDevice* device, IComputePipeline* pipeline)
{
    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();

    auto passEncoder = commandEncoder->beginComputePass();
    passEncoder->bindPipeline(pipeline);
    passEncoder->dispatchCompute(1, 1, 1);
    passEncoder->end();

    REQUIRE_CALL(queue->submit(commandEncoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
}

inline const CompilationReport* getCompilationReport(IShaderProgram* shaderProgram)
{
    ComPtr<ISlangBlob> reportBlob;
    REQUIRE_CALL(shaderProgram->getCompilationReport(reportBlob.writeRef()));
    s_blobs.push_back(reportBlob);
    CHECK(reportBlob->getBufferSize() >= sizeof(CompilationReport));
    const CompilationReport* report = (const CompilationReport*)reportBlob->getBufferPointer();
    size_t expectedSize = sizeof(CompilationReport);
    expectedSize += report->entryPointReportCount * sizeof(CompilationReport::EntryPointReport);
    expectedSize += report->pipelineReportCount * sizeof(CompilationReport::PipelineReport);
    CHECK(reportBlob->getBufferSize() == expectedSize);
    return report;
}

inline const CompilationReportList* getCompilationReportList(IDevice* device)
{
    ComPtr<ISlangBlob> reportListBlob;
    REQUIRE_CALL(device->getCompilationReportList(reportListBlob.writeRef()));
    s_blobs.push_back(reportListBlob);
    CHECK(reportListBlob->getBufferSize() >= sizeof(CompilationReportList));
    const CompilationReportList* reportList = (const CompilationReportList*)reportListBlob->getBufferPointer();
    size_t expectedSize = sizeof(CompilationReportList);
    for (uint32_t i = 0; i < reportList->reportCount; ++i)
    {
        expectedSize += sizeof(CompilationReport);
        expectedSize += reportList->reports[i].entryPointReportCount * sizeof(CompilationReport::EntryPointReport);
        expectedSize += reportList->reports[i].pipelineReportCount * sizeof(CompilationReport::PipelineReport);
    }
    CHECK(reportListBlob->getBufferSize() == expectedSize);
    return reportList;
}

GPU_TEST_CASE("compilation-report", ALL | DontCreateDevice)
{
    // On CPU backend, compilation is done late during pipeline creation.
    // Skip for now, as report is missing compilation times.
    if (ctx->deviceType == DeviceType::CPU)
    {
        return;
    }

    DeviceExtraOptions options = {};
    options.enableCompilationReports = true;
    device = createTestingDevice(ctx, ctx->deviceType, false, &options);
    REQUIRE(device);

    // Create first shader program.
    ComPtr<IShaderProgram> shaderProgram1 = createShaderProgram(device.get());

    // We expect no compilation has taken place yet, so the report should be empty.
    const CompilationReport* report1a = getCompilationReport(shaderProgram1.get());
    CHECK(report1a->alive == true);
    CHECK(report1a->createTime == 0.0);
    CHECK(report1a->compileTime == 0.0);
    CHECK(report1a->compileSlangTime == 0.0);
    CHECK(report1a->compileDownstreamTime == 0.0);
    CHECK(report1a->createPipelineTime == 0.0);
    CHECK(report1a->entryPointReportCount == 0);
    CHECK(report1a->pipelineReportCount == 0);

    // The report should be registered in the device.
    const CompilationReportList* reports1a = getCompilationReportList(device.get());
    CHECK(reports1a->reportCount == 1);
    CHECK(isEqual(&reports1a->reports[0], report1a));

    // Create and dispatch first pipeline.
    ComPtr<IComputePipeline> pipeline1 = createPipeline(device.get(), shaderProgram1.get());
    dispatchPipeline(device.get(), pipeline1.get());

    // We expect compilation and pipeline creation has taken place, so the report should contain non-zero times.
    const CompilationReport* report1b = getCompilationReport(shaderProgram1.get());
    CHECK(report1b->alive == true);
    CHECK(report1b->createTime > 0.0);
    CHECK(report1b->compileTime > 0.0);
    CHECK(report1b->compileSlangTime > 0.0);
    // Downstream compilation time may be zero if no downstream compiler is used.
    CHECK(report1b->compileDownstreamTime >= 0.0);
    CHECK(report1b->createPipelineTime > 0.0);
    CHECK(report1b->entryPointReportCount == 1);
    CHECK(report1b->pipelineReportCount == 1);

    // The report should still be registered in the device.
    const CompilationReportList* reports1b = getCompilationReportList(device.get());
    CHECK(reports1b->reportCount == 1);
    CHECK(isEqual(&reports1b->reports[0], report1b));

    // Create second shader program.
    ComPtr<IShaderProgram> shaderProgram2 = createShaderProgram(device.get());

    // We expect no compilation has taken place yet, so the report should be empty.
    const CompilationReport* report2a = getCompilationReport(shaderProgram2.get());
    CHECK(report2a->alive == true);
    CHECK(report2a->createTime == 0.0);
    CHECK(report2a->compileTime == 0.0);
    CHECK(report2a->compileSlangTime == 0.0);
    CHECK(report2a->compileDownstreamTime == 0.0);
    CHECK(report2a->createPipelineTime == 0.0);
    CHECK(report2a->entryPointReportCount == 0);
    CHECK(report2a->pipelineReportCount == 0);

    // The report should be registered in the device.
    const CompilationReportList* reports2a = getCompilationReportList(device.get());
    CHECK(reports2a->reportCount == 2);
    CHECK(isEqual(&reports2a->reports[0], report1b));
    CHECK(isEqual(&reports2a->reports[1], report2a));

    // Create and dispatch second pipeline.
    ComPtr<IComputePipeline> pipeline2 = createPipeline(device.get(), shaderProgram2.get());
    dispatchPipeline(device.get(), pipeline2.get());

    // We expect compilation and pipeline creation has taken place, so the report should contain non-zero times.
    const CompilationReport* report2b = getCompilationReport(shaderProgram2.get());
    CHECK(report2b->alive == true);
    CHECK(report2b->createTime > 0.0);
    CHECK(report2b->compileTime > 0.0);
    CHECK(report2b->compileSlangTime > 0.0);
    // Downstream compilation time may be zero if no downstream compiler is used.
    CHECK(report2b->compileDownstreamTime >= 0.0);
    CHECK(report2b->createPipelineTime > 0.0);
    CHECK(report2b->entryPointReportCount == 1);
    CHECK(report2b->pipelineReportCount == 1);

    // The report should still be registered in the device.
    const CompilationReportList* reports2b = getCompilationReportList(device.get());
    CHECK(reports2b->reportCount == 2);
    CHECK(isEqual(&reports2b->reports[0], report1b));
    CHECK(isEqual(&reports2b->reports[1], report2b));

    // Release the first shader program and pipeline.
    shaderProgram1 = nullptr;
    pipeline1 = nullptr;

    // The report first the first program should still be returned, but marked as no longer alive.
    const CompilationReportList* reports3 = getCompilationReportList(device.get());
    CompilationReport report1c = *report1b;
    report1c.alive = false; // The first program is no longer alive.
    CHECK(reports3->reportCount == 2);
    CHECK(isEqual(&reports3->reports[0], &report1c));
    CHECK(isEqual(&reports3->reports[1], report2b));
}
