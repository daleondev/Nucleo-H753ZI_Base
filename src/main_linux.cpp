#include "platform/platform.h"

#include <tx_api.h>

int main()
{
    tx_kernel_enter();
    platform::FatalError();
}