#pragma once

#include "utils.h"
#include "font-monogram.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <array>
#include <cstdint>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace rhi::bitmap_font {

// Printable ASCII in 6x9 cells, including lowercase. Unsupported bytes use '?'.
inline constexpr uint32_t kWidth = 16 * kGlyphWidth, kHeight = 6 * kGlyphHeight;

inline unsigned char glyphCharacter(unsigned char c)
{
    return c >= ' ' && c <= '~' ? c : '?';
}

inline std::array<uint8_t, kWidth * kHeight> createAtlas()
{
    std::array<uint8_t, kWidth * kHeight> pixels = {};
    for (uint32_t index = 0; index < std::size(kGlyphs); ++index)
    {
        uint32_t x = (index % 16) * kGlyphWidth, y = (index / 16) * kGlyphHeight;
        for (uint32_t row = 0; row < kGlyphHeight; ++row)
            for (uint32_t column = 0; column < kGlyphWidth; ++column)
                pixels[(y + row) * kWidth + x + column] = (kGlyphs[index][row] & (1 << column)) ? 255 : 0;
    }
    return pixels;
}

} // namespace rhi::bitmap_font

namespace rhi {

// A fixed-width text grid with a translucent background panel. Call clear(),
// append(), then render() after tone mapping. Layout uses the target dimensions
// and clips to the screen; contentScale controls the font size and padding.
// Raster and compute paths share the same glyph lookup and premultiplied colors.
class TextRenderer
{
public:
    static constexpr uint32_t kMaxCells = 4096;
    math::float4 textColor = {0.82f, 0.88f, 0.96f, 1};
    math::float4 panelColor = {0.003f, 0.006f, 0.012f, 0.85f};
    float margin = 12.f;

    Result init(IDevice* device, Format outputFormat)
    {
        m_device = device;
        m_outputFormat = outputFormat;
        auto pixels = bitmap_font::createAtlas();
        TextureDesc texture = {};
        texture.size = {bitmap_font::kWidth, bitmap_font::kHeight, 1};
        texture.format = Format::R8Unorm;
        texture.usage = TextureUsage::ShaderResource;
        texture.label = "HUD font atlas";
        SubresourceData data = {};
        data.data = pixels.data();
        data.rowPitch = bitmap_font::kWidth;
        data.slicePitch = pixels.size();
        SLANG_RETURN_ON_FAIL(device->createTexture(texture, &data, m_atlas.writeRef()));
        BufferDesc buffer = {};
        buffer.size = kMaxCells * sizeof(uint32_t);
        buffer.usage = BufferUsage::ShaderResource | BufferUsage::CopyDestination;
        buffer.label = "HUD character grid";
        return device->createBuffer(buffer, nullptr, m_buffer.writeRef());
    }

    void clear() { m_text.clear(); }

    // Normalize once so wrapping and glyph lookup use the same character widths.
    void append(std::string_view text)
    {
        for (unsigned char c : text)
        {
            if (c == '\t')
                m_text.append(4, ' ');
            else if (c != '\r')
                m_text += c == '\n' ? '\n' : char(bitmap_font::glyphCharacter(c));
        }
    }

    Result render(ICommandEncoder* encoder, ITexture* target, float contentScale = 1.f)
    {
        if (m_text.empty())
            return SLANG_OK;
        if (!encoder || !target || !m_device || target->getDesc().format != m_outputFormat)
            return SLANG_E_INVALID_ARG;
        const auto& desc = target->getDesc();
        uint32_t width = desc.size.width, height = desc.size.height;
        float padding = margin * contentScale;
        float fontScale = contentScale;
        if (!width || !height || !std::isfinite(padding) || padding < 0 || !std::isfinite(fontScale) || fontScale <= 0)
            return SLANG_E_INVALID_ARG;
        // Whole framebuffer pixels preserve the bitmap's stroke widths at fractional DPI scales.
        fontScale = std::max(1.f, std::round(fontScale));
        padding = std::round(padding);
        if (padding >= width || padding >= height)
            return SLANG_OK;

        // Bound the grid by both the screen and its fixed upload buffer. Only
        // complete rows with bottom panel padding fit; long words wrap at the column limit.
        uint32_t maxColumns = uint32_t(
            std::clamp(
                std::floor((width - 4 * padding) / (bitmap_font::kGlyphWidth * fontScale)),
                1.f,
                float(kMaxCells)
            )
        );
        uint32_t maxRows = uint32_t(
            std::clamp(
                std::floor((height - 3 * padding) / (bitmap_font::kGlyphHeight * fontScale)),
                0.f,
                float(kMaxCells / maxColumns)
            )
        );
        if (!maxRows)
            return SLANG_OK;
        std::vector<std::string_view> lines;
        std::string_view remaining(m_text);
        uint32_t columns = 1;
        while (!remaining.empty() && lines.size() < maxRows)
        {
            size_t newline = remaining.find('\n');
            auto line = remaining.substr(0, newline);
            size_t count = line.size();
            size_t skip = count + (newline != std::string_view::npos ? 1 : 0);
            if (count > maxColumns)
            {
                count = line.rfind(' ', maxColumns);
                if (count == std::string_view::npos || count == 0)
                    count = maxColumns;
                skip = count + (line[count] == ' ' ? 1 : 0);
            }
            lines.push_back(line.substr(0, count));
            columns = std::max(columns, uint32_t(count));
            remaining.remove_prefix(skip);
        }
        uint32_t rows = uint32_t(lines.size());
        m_grid.assign(columns * rows, 0); // Atlas cell zero is a space.
        for (uint32_t row = 0; row < rows; ++row)
            for (uint32_t column = 0; column < lines[row].size(); ++column)
                m_grid[row * columns + column] = uint32_t(lines[row][column] - ' ');

        math::float2 panelOrigin(padding);
        math::float2 textOrigin(2 * padding);
        math::float2 panelEnd =
            textOrigin +
            math::float2(columns * bitmap_font::kGlyphWidth, rows * bitmap_font::kGlyphHeight) * fontScale +
            math::float2(padding);
        ScissorRect bounds = {
            uint32_t(std::floor(padding)),
            uint32_t(std::floor(padding)),
            uint32_t(std::min(float(width), std::ceil(panelEnd.x))),
            uint32_t(std::min(float(height), std::ceil(panelEnd.y))),
        };
        bool raster = m_device->hasFeature(Feature::Rasterization) && is_set(desc.usage, TextureUsage::RenderTarget);
        if (!raster && !is_set(desc.usage, TextureUsage::UnorderedAccess))
            return SLANG_E_NOT_AVAILABLE;
        SLANG_RETURN_ON_FAIL(preparePipeline(raster));
        // Upload and consumption share the command stream; no mapped writes or host waits.
        SLANG_RETURN_ON_FAIL(encoder->uploadBufferData(m_buffer, 0, m_grid.size() * sizeof(uint32_t), m_grid.data()));

        IRenderPassEncoder* renderPass = nullptr;
        IComputePassEncoder* computePass = nullptr;
        IShaderObject* root;
        if (raster)
        {
            RenderPassColorAttachment color = {};
            color.view = target->getDefaultView();
            color.loadOp = LoadOp::Load;
            RenderPassDesc pass = {};
            pass.colorAttachments = &color;
            pass.colorAttachmentCount = 1;
            renderPass = encoder->beginRenderPass(pass);
            root = renderPass->bindPipeline(m_renderPipeline);
        }
        else
        {
            computePass = encoder->beginComputePass();
            root = computePass->bindPipeline(m_computePipeline);
        }
        ShaderCursor cursor(root);
        const auto& format = getFormatInfo(m_outputFormat);
        // Match raster blending: sRGB attachments blend in linear space; other
        // normalized presentation images store encoded values and blend as stored.
        uint32_t encodeSrgb = format.kind == FormatKind::Normalized && !format.isSrgb;
        uint32_t gridSize[] = {columns, rows};
        uint32_t glyphSize[] = {bitmap_font::kGlyphWidth, bitmap_font::kGlyphHeight};
        SLANG_RETURN_ON_FAIL(cursor["fontAtlas"].setBinding(m_atlas));
        SLANG_RETURN_ON_FAIL(cursor["characters"].setBinding(m_buffer));
        SLANG_RETURN_ON_FAIL(cursor["gridSize"].setData(gridSize));
        SLANG_RETURN_ON_FAIL(cursor["glyphSize"].setData(glyphSize));
        SLANG_RETURN_ON_FAIL(cursor["panelOrigin"].setData(panelOrigin));
        SLANG_RETURN_ON_FAIL(cursor["panelEnd"].setData(panelEnd));
        SLANG_RETURN_ON_FAIL(cursor["textOrigin"].setData(textOrigin));
        SLANG_RETURN_ON_FAIL(cursor["fontScale"].setData(fontScale));
        SLANG_RETURN_ON_FAIL(cursor["textColor"].setData(textColor));
        SLANG_RETURN_ON_FAIL(cursor["panelColor"].setData(panelColor));
        SLANG_RETURN_ON_FAIL(cursor["encodeSrgb"].setData(encodeSrgb));
        if (renderPass)
        {
            RenderState state = {};
            state.viewports[0] = Viewport::fromSize(width, height);
            state.viewportCount = 1;
            state.scissorRects[0] = bounds;
            state.scissorRectCount = 1;
            renderPass->setRenderState(state);
            DrawArguments draw = {};
            draw.vertexCount = 3;
            renderPass->draw(draw);
            renderPass->end();
        }
        else
        {
            uint32_t origin[] = {bounds.minX, bounds.minY};
            uint32_t extent[] = {bounds.maxX - bounds.minX, bounds.maxY - bounds.minY};
            uint32_t targetSrgb = format.isSrgb;
            SLANG_RETURN_ON_FAIL(cursor["target"].setBinding(target));
            SLANG_RETURN_ON_FAIL(cursor["dispatchOrigin"].setData(origin));
            SLANG_RETURN_ON_FAIL(cursor["dispatchExtent"].setData(extent));
            SLANG_RETURN_ON_FAIL(cursor["targetSrgb"].setData(targetSrgb));
            computePass->dispatchCompute(math::divRoundUp(extent[0], 8u), math::divRoundUp(extent[1], 8u), 1);
            computePass->end();
        }
        return SLANG_OK;
    }

private:
    Result preparePipeline(bool raster)
    {
        if (raster && !m_renderPipeline)
        {
            ColorTargetDesc color = {};
            color.format = m_outputFormat;
            color.enableBlend = true;
            color.color.srcFactor = color.alpha.srcFactor = BlendFactor::One;
            color.color.dstFactor = color.alpha.dstFactor = BlendFactor::InvSrcAlpha;
            RenderPipelineDesc desc = {};
            desc.targets = &color;
            desc.targetCount = 1;
            desc.depthStencil.depthWriteEnable = false;
            return createRenderPipeline(
                m_device,
                "../base/text-renderer.slang",
                "textVertex",
                "textFragment",
                desc,
                m_renderPipeline.writeRef()
            );
        }
        if (!raster && !m_computePipeline)
        {
            const char* format = getFormatInfo(m_outputFormat).slangName;
            if (m_outputFormat == Format::RGBA8UnormSrgb)
                format = "rgba8"; // Storage accesses use the underlying unorm representation.
            if (!format)
                return SLANG_E_NOT_AVAILABLE;
            std::string source = std::string("#define TEXT_TARGET_FORMAT \"") + format + "\"\n";
            if (m_outputFormat == Format::RGBA8Unorm || m_outputFormat == Format::RGBA8UnormSrgb)
                source += "#define TEXT_TARGET_RGBA8 1\n";
            source += "#include \"../base/text-renderer.slang\"\n";
            return createComputePipelineFromSource(
                m_device,
                source.c_str(),
                "textCompute",
                m_computePipeline.writeRef()
            );
        }
        return SLANG_OK;
    }

    ComPtr<IDevice> m_device;
    Format m_outputFormat = Format::Undefined;
    std::string m_text;
    std::vector<uint32_t> m_grid;
    ComPtr<ITexture> m_atlas;
    ComPtr<IBuffer> m_buffer;
    ComPtr<IRenderPipeline> m_renderPipeline;
    ComPtr<IComputePipeline> m_computePipeline;
};

} // namespace rhi
