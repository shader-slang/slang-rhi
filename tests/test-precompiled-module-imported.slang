module "test-precompiled-module-imported";

__include "test-precompiled-module-included.slang";

namespace ns
{
    public int helperFunc()
    {
        return 1;
    }
}
