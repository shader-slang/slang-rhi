// debug-shader-program.cpp
#include "debug-shader-program.h"

namespace rhi
{
using namespace Slang;

namespace debug
{

slang::TypeReflection* DebugShaderProgram::findTypeByName(const char* name)
{
    return baseObject->findTypeByName(name);
}

} // namespace debug
} // namespace rhi
