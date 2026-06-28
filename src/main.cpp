#include "hal/hal.hpp"

#include <chrono>
#include <thread>

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
    using namespace std::chrono_literals;

    while (true) {
        if (BSP_LED_Toggle(LED_GREEN) != BSP_ERROR_NONE ||
            BSP_LED_Toggle(LED_RED) != BSP_ERROR_NONE) {
            Error_Handler();
        }
        std::this_thread::sleep_for(100ms);
    }
}
