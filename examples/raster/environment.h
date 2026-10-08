#pragma once

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <algorithm>

namespace rhi::raster {

// Startup-only preprocessing. Compute writes ordinary array textures, then copies
// the faces to sampled cubemaps, including on backends without cube UAV views.
class Environment
{
public:
    static constexpr uint32_t kSpecularSize = 256;
    static constexpr uint32_t kSpecularMips = 9;
    static constexpr uint32_t kDiffuseSize = 16;
    static constexpr uint32_t kBrdfSize = 128;

    Result init(IDevice* device, IShaderProgram* filterProgram, IShaderProgram* brdfProgram)
    {
        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = filterProgram;
        ComPtr<IComputePipeline> filterPipeline;
        SLANG_RETURN_ON_FAIL(device->createComputePipeline(pipelineDesc, filterPipeline.writeRef()));
        pipelineDesc.program = brdfProgram;
        ComPtr<IComputePipeline> brdfPipeline;
        SLANG_RETURN_ON_FAIL(device->createComputePipeline(pipelineDesc, brdfPipeline.writeRef()));

        TextureDesc desc = {};
        desc.format = Format::RGBA16Float;
        desc.size = {kBrdfSize, kBrdfSize, 1};
        desc.usage = TextureUsage::ShaderResource | TextureUsage::UnorderedAccess;
        desc.label = "Environment BRDF LUT";
        SLANG_RETURN_ON_FAIL(device->createTexture(desc, nullptr, brdf.writeRef()));

        ComPtr<ICommandQueue> queue;
        SLANG_RETURN_ON_FAIL(device->getQueue(QueueType::Graphics, queue.writeRef()));
        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        pass->pushDebugGroup("Integrate environment BRDF", {});
        ShaderCursor cursor(pass->bindPipeline(brdfPipeline));
        SLANG_RETURN_ON_FAIL(cursor["brdfOutput"].setBinding(brdf));
        pass->dispatchCompute((kBrdfSize + 7) / 8, (kBrdfSize + 7) / 8, 1);
        pass->popDebugGroup();
        pass->end();

        // Keep temporary textures/views alive until the initialization submission finishes.
        ComPtr<ITexture> staging[2];
        ComPtr<ITextureView> views[2][kSpecularMips];
        for (uint32_t diffuseFilter = 0; diffuseFilter < 2; ++diffuseFilter)
        {
            uint32_t size = diffuseFilter ? kDiffuseSize : kSpecularSize;
            uint32_t mipCount = diffuseFilter ? 1 : kSpecularMips;
            desc.type = TextureType::Texture2DArray;
            desc.size = {size, size, 1};
            desc.arrayLength = 6;
            desc.mipCount = mipCount;
            desc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
            desc.label = "Environment filter staging";
            SLANG_RETURN_ON_FAIL(device->createTexture(desc, nullptr, staging[diffuseFilter].writeRef()));
            for (uint32_t mip = 0; mip < mipCount; ++mip)
            {
                TextureViewDesc view = {};
                view.subresourceRange = {0, 6, mip, 1};
                SLANG_RETURN_ON_FAIL(
                    device->createTextureView(staging[diffuseFilter], view, views[diffuseFilter][mip].writeRef())
                );
            }

            desc.type = TextureType::TextureCube;
            desc.arrayLength = 1;
            desc.usage = TextureUsage::ShaderResource | TextureUsage::CopyDestination;
            for (uint32_t preset = 0; preset < 2; ++preset)
            {
                auto& texture = diffuseFilter ? diffuse[preset] : specular[preset];
                desc.label = diffuseFilter ? "Environment diffuse irradiance" : "Environment GGX reflection";
                SLANG_RETURN_ON_FAIL(device->createTexture(desc, nullptr, texture.writeRef()));
                for (uint32_t mip = 0; mip < mipCount; ++mip)
                {
                    uint32_t mipSize = std::max(size >> mip, 1u);
                    float roughness = diffuseFilter ? 0 : float(mip) / (kSpecularMips - 1);
                    pass = encoder->beginComputePass();
                    pass->pushDebugGroup(diffuseFilter ? "Filter diffuse environment" : "Filter GGX environment", {});
                    cursor = ShaderCursor(pass->bindPipeline(filterPipeline));
                    SLANG_RETURN_ON_FAIL(cursor["environmentOutput"].setBinding(views[diffuseFilter][mip]));
                    SLANG_RETURN_ON_FAIL(cursor["environmentPreset"].setData(preset));
                    SLANG_RETURN_ON_FAIL(cursor["diffuseFilter"].setData(diffuseFilter));
                    SLANG_RETURN_ON_FAIL(cursor["filterRoughness"].setData(roughness));
                    pass->dispatchCompute((mipSize + 7) / 8, (mipSize + 7) / 8, 6);
                    pass->popDebugGroup();
                    pass->end();
                    encoder->copyTexture(
                        texture,
                        {0, 6, mip, 1},
                        {},
                        staging[diffuseFilter],
                        {0, 6, mip, 1},
                        {},
                        {mipSize, mipSize, 1}
                    );
                }
            }
        }
        ComPtr<ICommandBuffer> commands;
        SLANG_RETURN_ON_FAIL(encoder->finish(commands.writeRef()));
        SLANG_RETURN_ON_FAIL(queue->submit(commands));
        return queue->waitOnHost();
    }

    ComPtr<ITexture> specular[2], diffuse[2], brdf;
};

} // namespace rhi::raster
