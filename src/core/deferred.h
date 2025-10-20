#pragma once

namespace rhi {

template<typename Callback>
class Deferred
{
public:
    Deferred(Callback&& callback)
        : m_callback(callback)
    {
    }

    ~Deferred() { m_callback(); }

private:
    Callback m_callback;
};

#define SLANG_RHI_DEFERRED(block)                                                                                      \
    Deferred SLANG_CONCAT(_deferred, __COUNTER__)(                                                                     \
        [&]()                                                                                                          \
        {                                                                                                              \
            block                                                                                                      \
        }                                                                                                              \
    )

} // namespace rhi
