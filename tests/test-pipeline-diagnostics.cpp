#include "diagnostics-test.h"

#if SLANG_RHI_ENABLE_D3D12
#include "d3d12/d3d12-device.h"
#include "d3d12/d3d12-utils.h"

using namespace rhi;
using namespace rhi::testing;

#if SLANG_RHI_ENABLE_NVAPI
TEST_CASE("native-diagnostics-nvapi-status")
{
    DiagnosticCapture capture;
    RefPtr<d3d12::DeviceImpl> device = new d3d12::DeviceImpl();
    device->m_debugCallback = &capture;
    CHECK(
        NVAPIUtil::handleFail(NVAPI_ERROR, "mock.cpp", 42, "NvAPI_D3D12_CreateComputePipelineState", device) ==
        SLANG_FAIL
    );
    REQUIRE(capture.messages.size() == 1);
    CHECK(capture.messages[0].text.find("-1 (NVAPI status)") != std::string::npos);
    CHECK(capture.messages[0].text.find("NvAPI_D3D12_CreateComputePipelineState") != std::string::npos);
    CHECK(capture.messages[0].text.find("mock.cpp:42") != std::string::npos);
}
#endif
#endif
