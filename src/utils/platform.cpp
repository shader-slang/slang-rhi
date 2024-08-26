#include "platform.h"

#include "assert.h"

namespace gfx {

SlangResult loadSharedLibrary(const char* path, SharedLibraryHandle& handleOut)
{
    SLANG_RHI_ASSERT_FAILURE("Not implemented");
}

void unloadSharedLibrary(SharedLibraryHandle handle)
{
    SLANG_RHI_ASSERT_FAILURE("Not implemented");
}

/// Given a shared library handle and a name, return the associated object
/// Return nullptr if object is not found
/// @param The shared library handle as returned by loadPlatformLibrary
void* findSymbolAddressByName(SharedLibraryHandle handle, char const* name)
{
    SLANG_RHI_ASSERT_FAILURE("Not implemented");
}

} // namespace gfx
