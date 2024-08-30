#include "debug-shader-program.h"

namespace rhi::debug {

slang::TypeReflection* DebugShaderProgram::findTypeByName(const char* name)
{
    return baseObject->findTypeByName(name);
}

} // namespace rhi::debug
