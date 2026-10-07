#include "testing.h"
#include "../examples/base/frame-stats.h"
#include "../examples/base/text-renderer.h"

using namespace rhi;
using namespace rhi::testing;

TEST_CASE("example-frame-stats")
{
    FrameStats stats;
    CHECK_FALSE(stats.update(10));
    CHECK(stats.fps() == 0);
    int publications = 0;
    for (int i = 1; i <= 120; ++i)
        publications += stats.update(10 + double(i) / 60);
    CHECK(publications >= 7);
    CHECK(publications <= 8);
    CHECK(stats.fps() == doctest::Approx(60));
    CHECK(stats.milliseconds() == doctest::Approx(1000.0 / 60));
    // Old samples age out of the rolling window when the frame rate changes.
    for (int i = 1; i <= 180; ++i)
        stats.update(12 + double(i) / 120);
    CHECK(stats.fps() == doctest::Approx(120));
    stats.reset();
    CHECK(stats.fps() == 0);
    CHECK_FALSE(stats.update(100));
    CHECK_FALSE(stats.update(99));
    CHECK(stats.milliseconds() == 0);
}

TEST_CASE("example-text-capacity")
{
    TextRenderer text;
    CHECK(text.addText("A", {0, 0}, 0) == SLANG_E_INVALID_ARG);
    CHECK(text.addText(std::string(TextRenderer::kMaxGlyphs + 1, 'A'), {0, 0}, 1) == SLANG_E_OUT_OF_MEMORY);
    REQUIRE_CALL(text.addText(std::string(TextRenderer::kMaxGlyphs, 'A'), {0, 0}, 1));
    REQUIRE_CALL(text.addText(" \n\r\t", {0, 0}, 1));
    CHECK(text.addText("B", {0, 0}, 1) == SLANG_E_OUT_OF_MEMORY);
    text.clear();
    REQUIRE_CALL(text.addText("B", {0, 0}, 1));
}

GPU_TEST_CASE("example-text-render", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    if (!device->hasFeature(Feature::Rasterization))
        SKIP("Rasterization is not supported");
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-example-text", {"textVertex", "textFragment"}, program.writeRef()));
    auto queue = device->getQueue(QueueType::Graphics);
    for (Format format : {Format::RGBA8Unorm, Format::RGBA8UnormSrgb})
    {
        TextRenderer text;
        REQUIRE_CALL(text.init(device, format, program));
        TextureDesc desc = {};
        desc.size = {96, 48, 1};
        desc.format = format;
        desc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
        auto target = device->createTexture(desc);
        REQUIRE(target);
        auto render = [&](std::string_view label)
        {
            text.clear();
            REQUIRE_CALL(text.addText(label, {2, 2}, 2, {1, 1, 1, 0.5f}));
            auto encoder = queue->createCommandEncoder();
            RenderPassColorAttachment color = {};
            color.view = target->getDefaultView();
            color.clearValue[0] = 0.1f;
            color.clearValue[1] = 0.2f;
            color.clearValue[2] = 0.3f;
            color.clearValue[3] = 1;
            RenderPassDesc pass = {};
            pass.colorAttachments = &color;
            pass.colorAttachmentCount = 1;
            encoder->beginRenderPass(pass)->end();
            REQUIRE_CALL(text.render(encoder, target));
            ComPtr<ICommandBuffer> commands;
            REQUIRE_CALL(encoder->finish(commands.writeRef()));
            REQUIRE_CALL(queue->submit(commands));
            REQUIRE_CALL(queue->waitOnHost());
            ComPtr<ISlangBlob> pixels;
            SubresourceLayout layout;
            REQUIRE_CALL(device->readTexture(target, 0, 0, pixels.writeRef(), &layout));
            std::vector<uint8_t> result(96 * 48 * 4);
            for (uint32_t y = 0; y < 48; ++y)
                std::memcpy(
                    result.data() + y * 96 * 4,
                    static_cast<const uint8_t*>(pixels->getBufferPointer()) + y * layout.rowPitch,
                    96 * 4
                );
            return result;
        };
        auto pixels = render("A\nB");
        auto expected = [&](float linear)
        {
            if (getFormatInfo(format).isSrgb)
                linear = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
            return int(std::round(linear * 255));
        };
        for (int channel = 0; channel < 3; ++channel)
        {
            float background = 0.1f * (channel + 1);
            // Atlas holes and pixels outside the text preserve the loaded target.
            CHECK(std::abs(int(pixels[(2 * 96 + 2) * 4 + channel]) - expected(background)) <= 1);
            CHECK(std::abs(int(pixels[(47 * 96 + 95) * 4 + channel]) - expected(background)) <= 1);
            CHECK(std::abs(int(pixels[(2 * 96 + 4) * 4 + channel]) - expected(0.5f + 0.5f * background)) <= 1);
            CHECK(std::abs(int(pixels[(20 * 96 + 2) * 4 + channel]) - expected(0.5f + 0.5f * background)) <= 1);
        }
        CHECK(pixels[(2 * 96 + 4) * 4 + 3] == 255);
        CHECK(render("a\nb") == pixels);
        CHECK(render("?") == render("\x01"));
        // Updating the same buffer and then clearing the batch must not retain glyphs.
        auto empty = render("");
        CHECK(empty != pixels);
        CHECK(empty[(2 * 96 + 4) * 4] == empty[0]);
    }
}
