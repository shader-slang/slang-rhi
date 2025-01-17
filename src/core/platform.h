#pragma once

#include <slang-rhi.h>

namespace rhi {

using SharedLibraryHandle = void*;

Result loadSharedLibrary(const char* path, SharedLibraryHandle& handleOut);
void unloadSharedLibrary(SharedLibraryHandle handle);

/// Given a shared library handle and a name, return the associated object.
/// Return nullptr if object is not found.
void* findSymbolAddressByName(SharedLibraryHandle handle, const char* name);

/// Given a symbol from a loaded shared library, return the library's path.
const char* findSharedLibraryPath(void* symbolAddress);

} // namespace rhi
