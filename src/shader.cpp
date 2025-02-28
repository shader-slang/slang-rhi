#include "shader.h"

#include "rhi-shared.h"
#include "shader-object.h"

namespace rhi {

SpecializationKey::SpecializationKey(const ExtendedShaderObjectTypeList& args)
    : componentIDs(args.componentIDs)
{
}

}
