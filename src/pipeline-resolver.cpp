#include "pipeline-resolver.h"

#include "command-list.h"
#include "device.h"
#include "pipeline.h"
#include "shader-object.h"

namespace rhi {

Result resolvePipelines(Device* device, CommandList* commandList)
{
    auto command = commandList->getCommands();
    while (command)
    {
        if (command->id == CommandID::SetRenderState)
        {
            auto& cmd = commandList->getCommand<commands::SetRenderState>(command);
            RenderPipeline* pipeline = checked_cast<RenderPipeline*>(cmd.pipeline);
            auto specializationArgs = static_cast<ExtendedShaderObjectTypeListObject*>(cmd.specializationArgs);
            Pipeline* concretePipeline = nullptr;
            SLANG_RETURN_ON_FAIL(device->getConcretePipeline(pipeline, specializationArgs, concretePipeline));
            cmd.pipeline = static_cast<RenderPipeline*>(concretePipeline);
            cmd.specializationArgs = nullptr;
        }
        else if (command->id == CommandID::SetComputeState)
        {
            auto& cmd = commandList->getCommand<commands::SetComputeState>(command);
            ComputePipeline* pipeline = checked_cast<ComputePipeline*>(cmd.pipeline);
            auto specializationArgs = static_cast<ExtendedShaderObjectTypeListObject*>(cmd.specializationArgs);
            Pipeline* concretePipeline = nullptr;
            SLANG_RETURN_ON_FAIL(device->getConcretePipeline(pipeline, specializationArgs, concretePipeline));
            cmd.pipeline = static_cast<ComputePipeline*>(concretePipeline);
            cmd.specializationArgs = nullptr;
        }
        else if (command->id == CommandID::SetRayTracingState)
        {
            auto& cmd = commandList->getCommand<commands::SetRayTracingState>(command);
            RayTracingPipeline* pipeline = checked_cast<RayTracingPipeline*>(cmd.pipeline);
            auto specializationArgs = static_cast<ExtendedShaderObjectTypeListObject*>(cmd.specializationArgs);
            Pipeline* concretePipeline = nullptr;
            SLANG_RETURN_ON_FAIL(device->getConcretePipeline(pipeline, specializationArgs, concretePipeline));
            cmd.pipeline = static_cast<RayTracingPipeline*>(concretePipeline);
            cmd.specializationArgs = nullptr;
        }
        command = command->next;
    }
    return SLANG_OK;
}

} // namespace rhi
