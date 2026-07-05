#include "filex/ram_disk_sample.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

extern "C" VOID _fx_ram_driver(FX_MEDIA* media_ptr);

namespace
{
    constexpr ULONG SECTOR_SIZE{ 512U };
    constexpr ULONG TOTAL_SECTORS{ 64U };
    constexpr ULONG SECTORS_PER_CLUSTER{ 4U };
    constexpr std::size_t RAM_DISK_SIZE{ SECTOR_SIZE * TOTAL_SECTORS };
    constexpr std::size_t MEDIA_CACHE_SIZE{ SECTOR_SIZE * 2U };
    constexpr std::array<UCHAR, 29U> SAMPLE_CONTENT{
        'F', 'i', 'l', 'e', 'X', ' ', 'R', 'A', 'M', ' ', 'd', 'i', 's', 'k', ' ',
        'i', 's', ' ', 'w', 'o', 'r', 'k', 'i', 'n', 'g', '.', '\r', '\n', '\0'
    };

    // The linker gives the disk its own 32 KiB AXI SRAM region on STM32.
    // NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
    [[gnu::section(".filex_ram_disk"), gnu::used]] alignas(32)
        std::array<UCHAR, RAM_DISK_SIZE> ram_disk_memory{};
    alignas(32) std::array<UCHAR, MEDIA_CACHE_SIZE> media_cache{};
    FX_MEDIA media{};
    FX_FILE file{};
    std::array<CHAR, sizeof("SAMPLE.TXT")> file_name{ "SAMPLE.TXT" };
    std::array<CHAR, sizeof("RAM DISK")> volume_name{ "RAM DISK" };
    // NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

    auto closeMedia(UINT status) noexcept -> UINT
    {
        const UINT close_status{ fx_media_close(&media) };
        return status == FX_SUCCESS ? close_status : status;
    }
}

namespace sample
{
    auto runFilexRamDisk() noexcept -> UINT
    {
        fx_system_initialize();

        UINT status{ fx_media_format(&media,
                                     _fx_ram_driver,
                                     ram_disk_memory.data(),
                                     media_cache.data(),
                                     static_cast<ULONG>(media_cache.size()),
                                     volume_name.data(),
                                     1U,
                                     32U,
                                     0U,
                                     TOTAL_SECTORS,
                                     SECTOR_SIZE,
                                     SECTORS_PER_CLUSTER,
                                     1U,
                                     1U) };
        if (status != FX_SUCCESS) {
            return status;
        }

        status = fx_media_open(&media,
                               volume_name.data(),
                               _fx_ram_driver,
                               ram_disk_memory.data(),
                               media_cache.data(),
                               static_cast<ULONG>(media_cache.size()));
        if (status != FX_SUCCESS) {
            return status;
        }

        status = fx_file_create(&media, file_name.data());
        if (status != FX_SUCCESS) {
            return closeMedia(status);
        }

        status = fx_file_open(&media, &file, file_name.data(), FX_OPEN_FOR_WRITE);
        if (status != FX_SUCCESS) {
            return closeMedia(status);
        }

        constexpr ULONG CONTENT_SIZE{ SAMPLE_CONTENT.size() - 1U };
        status = fx_file_write(&file, const_cast<UCHAR*>(SAMPLE_CONTENT.data()), CONTENT_SIZE);
        const UINT file_close_status{ fx_file_close(&file) };
        if (status != FX_SUCCESS || file_close_status != FX_SUCCESS) {
            return closeMedia(status != FX_SUCCESS ? status : file_close_status);
        }

        status = fx_file_open(&media, &file, file_name.data(), FX_OPEN_FOR_READ);
        if (status != FX_SUCCESS) {
            return closeMedia(status);
        }

        std::array<UCHAR, SAMPLE_CONTENT.size()> read_buffer{};
        ULONG actual_size{};
        status = fx_file_read(&file, read_buffer.data(), CONTENT_SIZE, &actual_size);
        const bool content_matches{ status == FX_SUCCESS && actual_size == CONTENT_SIZE &&
                                    std::equal(SAMPLE_CONTENT.begin(), SAMPLE_CONTENT.end() - 1U,
                                               read_buffer.begin()) };
        const UINT read_close_status{ fx_file_close(&file) };
        if (!content_matches) {
            return closeMedia(status == FX_SUCCESS ? FX_IO_ERROR : status);
        }
        if (read_close_status != FX_SUCCESS) {
            return closeMedia(read_close_status);
        }

        return closeMedia(FX_SUCCESS);
    }
}
