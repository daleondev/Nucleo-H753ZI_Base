#include "platform/platform.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    bool green_led_on = false;
    bool red_led_on = false;
}

namespace platform
{
    void ToggleDemoIndicators()
    {
        green_led_on = !green_led_on;
        red_led_on = !red_led_on;

        std::printf("[sim] green=%s red=%s\n", green_led_on ? "on" : "off", red_led_on ? "on" : "off");
        std::fflush(stdout);
    }

    [[noreturn]] void ReportStackOverflow(const char* thread_name)
    {
        std::fprintf(stderr,
                     "[sim] Thread %s stack overflow detected\n",
                     thread_name != nullptr ? thread_name : "Unknown");
        std::fflush(stderr);
        std::abort();
    }

    [[noreturn]] void FatalError()
    {
        std::fprintf(stderr, "[sim] Fatal error\n");
        std::fflush(stderr);
        std::abort();
    }
}