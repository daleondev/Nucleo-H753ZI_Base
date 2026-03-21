#pragma once

namespace platform
{
    void ToggleDemoIndicators();
    [[noreturn]] void ReportStackOverflow(const char* thread_name);
    [[noreturn]] void FatalError();
}