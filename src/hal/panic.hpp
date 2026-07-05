#pragma once

#include "hal/panic.h"

#include <source_location>

namespace hal
{
    [[noreturn]] auto panic(
      const char* message,
      std::source_location location = std::source_location::current()) noexcept -> void;
}
