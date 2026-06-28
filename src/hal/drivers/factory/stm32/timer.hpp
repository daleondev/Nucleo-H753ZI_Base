#pragma once

#include "hal/drivers/itf/ITimer.hpp"

namespace hal::timer
{
    std::shared_ptr<ITimer> create(size_t index);
}
