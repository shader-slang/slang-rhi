#pragma once

#include <slang.h>

namespace rhi {

using SharedLibraryHandle = void*;

SlangResult loadSharedLibrary(const char* path, SharedLibraryHandle& handleOut);
void unloadSharedLibrary(SharedLibraryHandle handle);

/// Given a shared library handle and a name, return the associated object.
/// Return nullptr if object is not found.
void* findSymbolAddressByName(SharedLibraryHandle handle, char const* name);

} // namespace rhi
