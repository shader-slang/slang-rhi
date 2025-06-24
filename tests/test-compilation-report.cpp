#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

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

            ComPtr<IShaderProgram> shaderProgram;
            loadComputeProgramFromSource(device, shaderProgram, R"(
    [shader("compute")]
    [numthreads(1, 1, 1)]
    void computeMain() {}
    )");

            ComPtr<ISlangBlob> reportBlob;
            REQUIRE_CALL(shaderProgram->getCompilationReport(CompilationReportType::Struct, reportBlob.writeRef()));
            CHECK(reportBlob->getBufferSize() == sizeof(CompilationReport));
            const CompilationReport* report = (const CompilationReport*)reportBlob->getBufferPointer();

            // We expect no compilation has taken place yet, so the report should be empty.
            CHECK(report->createTime == 0.0);
            CHECK(report->compileTime == 0.0);
            CHECK(report->compileSlangTime == 0.0);
            CHECK(report->compileDownstreamTime == 0.0);
            CHECK(report->createPipelineTime == 0.0);

            ComputePipelineDesc pipelineDesc = {};
            pipelineDesc.program = shaderProgram.get();
            ComPtr<IComputePipeline> pipeline;
            REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

            {
                auto queue = device->getQueue(QueueType::Graphics);
                auto commandEncoder = queue->createCommandEncoder();

                auto passEncoder = commandEncoder->beginComputePass();
                auto rootObject = passEncoder->bindPipeline(pipeline);
                passEncoder->dispatchCompute(1, 1, 1);
                passEncoder->end();

                queue->submit(commandEncoder->finish());
                queue->waitOnHost();
            }

            REQUIRE_CALL(shaderProgram->getCompilationReport(CompilationReportType::Struct, reportBlob.writeRef()));
            CHECK(reportBlob->getBufferSize() == sizeof(CompilationReport));
            report = (const CompilationReport*)reportBlob->getBufferPointer();

            // We expect compilation and pipeline creation has taken place, so the report should contain non-zero times.
            CHECK(report->createTime > 0.0);
            CHECK(report->compileTime > 0.0);
            CHECK(report->compileSlangTime > 0.0);
            // Downstream compilation time may be zero if no downstream compiler is used.
            CHECK(report->compileDownstreamTime >= 0.0);
            CHECK(report->createPipelineTime > 0.0);
        }
    );
}
