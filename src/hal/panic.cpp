#include "hal/panic.hpp"

namespace hal
{
    auto panic(const char* message, std::source_location location) noexcept -> void
    {
        const HalPanicInfo info{
            .message = message,
            .file = location.file_name(),
            .function = location.function_name(),
            .line = location.line(),
        };
        hal_panic_handler(&info);
    }
}
