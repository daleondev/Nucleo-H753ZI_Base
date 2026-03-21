#include "platform/platform.h"

#include "main.h"

namespace platform
{
    void ToggleDemoIndicators()
    {
        BSP_LED_Toggle(LED_GREEN);
        BSP_LED_Toggle(LED_RED);
    }

    [[noreturn]] void ReportStackOverflow(const char* thread_name)
    {
        printf("Thread %s stack overflow detected\r\n", thread_name != nullptr ? thread_name : "Unknown");
        Error_Handler();

        while (true) {
        }
    }

    [[noreturn]] void FatalError()
    {
        Error_Handler();

        while (true) {
        }
    }
}