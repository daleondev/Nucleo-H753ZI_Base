#include "filex/ram_disk_sample.hpp"

#include <gtest/gtest.h>

TEST(FileX, RamDiskRoundTrip)
{
    EXPECT_EQ(sample::runFilexRamDisk(), FX_SUCCESS);
}
