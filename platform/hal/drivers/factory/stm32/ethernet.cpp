#include "hal/drivers/factory/ethernet.hpp"

#include "hal/drivers/impl/stm32/Ethernet.hpp"
#include "hal/hal.hpp"

#include <memory>

namespace hal::ethernet
{
    auto create(IEthernet::Configuration configuration) -> std::shared_ptr<IEthernet>
    {
        static std::weak_ptr<IEthernet> existing;
        if (auto driver{ existing.lock() }) {
            return driver;
        }

        auto driver{ std::make_shared<Ethernet>(
          Ethernet::HardwareConfiguration{ .handle = heth, .configuration = configuration }) };
        existing = driver;
        return driver;
    }
}
