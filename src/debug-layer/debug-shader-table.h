// debug-shader-table.h
#pragma once
#include "debug-base.h"

namespace rhi
{
using namespace Slang;

namespace debug
{

class DebugShaderTable : public DebugObject<IShaderTable>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;
    IShaderTable* getInterface(const Slang::Guid& guid);
};

} // namespace debug
} // namespace rhi
