#include "Mutex.hpp"

namespace osal
{
    auto Mutex::lock() -> void { m_mutex.lock(); }
    auto Mutex::unlock() -> void { m_mutex.unlock(); }
}