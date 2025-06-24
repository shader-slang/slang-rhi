#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

inline ComPtr<IShaderProgram> createShaderProgram(IDevice* device)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadComputeProgramFromSource(device, shaderProgram, R"(
    [shader("compute")]
    [numthreads(1, 1, 1)]
    void computeMain() {}
    )"));
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
    auto rootObject = passEncoder->bindPipeline(pipeline);
    passEncoder->dispatchCompute(1, 1, 1);
    passEncoder->end();

    REQUIRE_CALL(queue->submit(commandEncoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
}

inline CompilationReport getCompilationReport(IShaderProgram* shaderProgram)
{
    ComPtr<ISlangBlob> reportBlob;
    REQUIRE_CALL(shaderProgram->getCompilationReport(CompilationReportType::Struct, reportBlob.writeRef()));
    CHECK(reportBlob->getBufferSize() == sizeof(CompilationReport));
    return *(const CompilationReport*)reportBlob->getBufferPointer();
}

inline std::vector<CompilationReport> getCompilationReports(IDevice* device)
{
    ComPtr<ISlangBlob> reportBlob;
    REQUIRE_CALL(device->getCompilationReports(CompilationReportType::Struct, reportBlob.writeRef()));
    CHECK(reportBlob->getBufferSize() % sizeof(CompilationReport) == 0);
    size_t count = reportBlob->getBufferSize() / sizeof(CompilationReport);
    const CompilationReport* reports = (const CompilationReport*)reportBlob->getBufferPointer();
    return std::vector<CompilationReport>(reports, reports + count);
}

TEST_CASE("compilation-report")
{
    runGpuTests(
        [](GpuTestContext* ctx, DeviceType deviceType)
        {
            // On CPU backend, compilation is done late during pipeline creation.
            // Skip for now, as report is missing compilation times.
            if (deviceType == DeviceType::CPU)
            {
                return;
            }

            DeviceExtraOptions options = {};
            options.enableCompilationReports = true;
            ComPtr<IDevice> device = createTestingDevice(ctx, deviceType, false, &options);
            REQUIRE(device);

            // Create first shader program.
            ComPtr<IShaderProgram> shaderProgram1 = createShaderProgram(device.get());

            // We expect no compilation has taken place yet, so the report should be empty.
            CompilationReport report1a = getCompilationReport(shaderProgram1.get());
            CHECK(report1a.alive == true);
            CHECK(report1a.createTime == 0.0);
            CHECK(report1a.compileTime == 0.0);
            CHECK(report1a.compileSlangTime == 0.0);
            CHECK(report1a.compileDownstreamTime == 0.0);
            CHECK(report1a.createPipelineTime == 0.0);

            // The report should be registered in the device.
            std::vector<CompilationReport> reports1a = getCompilationReports(device.get());
            CHECK(reports1a.size() == 1);
            CHECK(std::memcmp(&reports1a[0], &report1a, sizeof(CompilationReport)) == 0);

            // Create and dispatch first pipeline.
            ComPtr<IComputePipeline> pipeline1 = createPipeline(device.get(), shaderProgram1.get());
            dispatchPipeline(device.get(), pipeline1.get());

            // We expect compilation and pipeline creation has taken place, so the report should contain non-zero times.
            CompilationReport report1b = getCompilationReport(shaderProgram1.get());
            CHECK(report1b.alive == true);
            CHECK(report1b.createTime > 0.0);
            CHECK(report1b.compileTime > 0.0);
            CHECK(report1b.compileSlangTime > 0.0);
            // Downstream compilation time may be zero if no downstream compiler is used.
            CHECK(report1b.compileDownstreamTime >= 0.0);
            CHECK(report1b.createPipelineTime > 0.0);

            // The report should still be registered in the device.
            std::vector<CompilationReport> reports1b = getCompilationReports(device.get());
            CHECK(reports1b.size() == 1);
            CHECK(std::memcmp(&reports1b[0], &report1b, sizeof(CompilationReport)) == 0);

            // Create second shader program.
            ComPtr<IShaderProgram> shaderProgram2 = createShaderProgram(device.get());

            // We expect no compilation has taken place yet, so the report should be empty.
            CompilationReport report2a = getCompilationReport(shaderProgram2.get());
            CHECK(report2a.alive == true);
            CHECK(report2a.createTime == 0.0);
            CHECK(report2a.compileTime == 0.0);
            CHECK(report2a.compileSlangTime == 0.0);
            CHECK(report2a.compileDownstreamTime == 0.0);
            CHECK(report2a.createPipelineTime == 0.0);

            // The report should be registered in the device.
            std::vector<CompilationReport> reports2a = getCompilationReports(device.get());
            CHECK(reports2a.size() == 2);
            CHECK(std::memcmp(&reports2a[0], &report1b, sizeof(CompilationReport)) == 0);
            CHECK(std::memcmp(&reports2a[1], &report2a, sizeof(CompilationReport)) == 0);

            // Create and dispatch second pipeline.
            ComPtr<IComputePipeline> pipeline2 = createPipeline(device.get(), shaderProgram2.get());
            dispatchPipeline(device.get(), pipeline2.get());

            // We expect compilation and pipeline creation has taken place, so the report should contain non-zero times.
            CompilationReport report2b = getCompilationReport(shaderProgram2.get());
            CHECK(report2b.alive == true);
            CHECK(report2b.createTime > 0.0);
            CHECK(report2b.compileTime > 0.0);
            CHECK(report2b.compileSlangTime > 0.0);
            // Downstream compilation time may be zero if no downstream compiler is used.
            CHECK(report2b.compileDownstreamTime >= 0.0);
            CHECK(report2b.createPipelineTime > 0.0);

            // The report should still be registered in the device.
            std::vector<CompilationReport> reports2b = getCompilationReports(device.get());
            CHECK(reports2b.size() == 2);
            CHECK(std::memcmp(&reports2b[0], &report1b, sizeof(CompilationReport)) == 0);
            CHECK(std::memcmp(&reports2b[1], &report2b, sizeof(CompilationReport)) == 0);

            // Release the first shader program and pipeline.
            shaderProgram1 = nullptr;
            pipeline1 = nullptr;

            // The report first the first program should still be returned, but marked as no longer alive.
            std::vector<CompilationReport> reports3 = getCompilationReports(device.get());
            CompilationReport report1c = report1b;
            report1c.alive = false; // The first program is no longer alive.
            CHECK(reports3.size() == 2);
            CHECK(std::memcmp(&reports3[0], &report1c, sizeof(CompilationReport)) == 0);
            CHECK(std::memcmp(&reports3[1], &report2b, sizeof(CompilationReport)) == 0);
        }
    );
}
