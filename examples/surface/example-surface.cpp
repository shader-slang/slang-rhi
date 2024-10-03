#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <slang-rhi.h>
#include <slang-rhi/glfw.h>

using namespace rhi;

int main(int argc, const char** argv)
{
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Hello, World!", nullptr, nullptr);
    if (!window)
    {
        return 1;
    }

    IDevice::Desc deviceDesc;
    deviceDesc.deviceType = DeviceType::Metal;

    ComPtr<IDevice> device;
    if (rhiCreateDevice(&deviceDesc, device.writeRef()))
    {
        return 1;
    }

    ComPtr<ISurface> surface = device->createSurface(getWindowHandleFromGLFW(window));

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    SurfaceConfig surfaceConfig;
    surfaceConfig.width = width;
    surfaceConfig.height = height;
    surface->configure(surfaceConfig);

    glfwSetWindowUserPointer(window, surface.get());

    glfwSetFramebufferSizeCallback(
        window,
        [](GLFWwindow* window, int width, int height)
        {
            ISurface* surface = (ISurface*)glfwGetWindowUserPointer(window);
            SurfaceConfig surfaceConfig;
            surfaceConfig.width = width;
            surfaceConfig.height = height;
            surface->configure(surfaceConfig);
        }
    );

    ITransientResourceHeap::Desc transientHeapDesc = {};
    ComPtr<ITransientResourceHeap> transientHeap = device->createTransientResourceHeap(transientHeapDesc);

    ComPtr<ICommandQueue> queue = device->createCommandQueue({ICommandQueue::QueueType::Graphics});

    int frame = 0;
    float grey = 0.f;

    while (!glfwWindowShouldClose(window))
    {
        // printf("Frame %d\n", frame++);

        glfwPollEvents();

        ComPtr<ITexture> texture;
        surface->getCurrentTexture(texture.writeRef());
        if (!texture)
        {
            printf("Skipping frame\n");
            continue;
        }
        ComPtr<ITextureView> view = device->createTextureView(texture, {});

        auto commandBuffer = transientHeap->createCommandBuffer();
        RenderPassColorAttachment colorAttachment;
        colorAttachment.clearValue[0] = grey;
        colorAttachment.clearValue[1] = grey;
        colorAttachment.clearValue[2] = grey;
        colorAttachment.clearValue[3] = 1.f;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.view = view;
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;
        auto encoder = commandBuffer->beginRenderPass(renderPass);
        encoder->end();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);

        surface->present();

        grey = (grey + (1.f / 60.f)) > 1.f ? 0.f : grey + (1.f / 60.f);

        transientHeap->synchronizeAndReset();
    }

    glfwDestroyWindow(window);

    glfwTerminate();
    return 0;
}
