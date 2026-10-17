#include "base/example.h"

rhi::ExampleDesc getExampleSurface();
rhi::ExampleDesc getExampleTriangle();
rhi::ExampleDesc getExampleShaderToy();
rhi::ExampleDesc getExampleRaster();
rhi::ExampleDesc getExampleParticles();
#if !SLANG_WASM
rhi::ExampleDesc getExamplePathTracer();
#endif

int main(int argc, const char** argv)
{
    const rhi::ExampleDesc examples[] = {
        getExampleSurface(),
        getExampleTriangle(),
        getExampleShaderToy(),
        getExampleRaster(),
        getExampleParticles(),
#if !SLANG_WASM
        getExamplePathTracer(),
#endif
    };
    return rhi::detail::runExamples(argc, argv, examples);
}
