#include "smart-pointer.h"

namespace rhi {

#if SLANG_RHI_DEBUG
std::atomic<uint64_t> RefObject::s_objectCount = 0;
#endif

} // namespace rhi
