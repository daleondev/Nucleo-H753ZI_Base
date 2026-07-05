#include "hal/board/board.hpp"
#include "hal/drivers/factory/ethernet.hpp"
#include "hal/hal.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
    using namespace std::chrono_literals;

    constexpr std::size_t DESTINATION_OFFSET{ 0U };
    constexpr std::size_t SOURCE_OFFSET{ 6U };
    constexpr std::size_t ETHERCAT_HEADER_OFFSET{ 14U };
    constexpr std::size_t COMMAND_OFFSET{ 16U };
    constexpr std::size_t INDEX_OFFSET{ 17U };
    constexpr std::size_t ADDRESS_POSITION_OFFSET{ 18U };
    constexpr std::size_t ADDRESS_REGISTER_OFFSET{ 20U };
    constexpr std::size_t DATAGRAM_LENGTH_OFFSET{ 22U };
    constexpr std::size_t IRQ_OFFSET{ 24U };
    constexpr std::size_t DATA_OFFSET{ 26U };
    constexpr std::size_t WORKING_COUNTER_OFFSET{ 28U };
    constexpr std::size_t RESPONSE_SIZE{ 30U };

    constexpr std::uint16_t ETHERCAT_PROTOCOL_HEADER{ 0x100EU };
    constexpr std::uint8_t BROADCAST_READ_COMMAND{ 0x07U };
    constexpr std::uint16_t AL_STATUS_REGISTER{ 0x0130U };
    constexpr std::uint16_t AL_STATUS_SIZE{ 2U };
    constexpr std::uint16_t DATAGRAM_LENGTH_MASK{ 0x07FFU };
    constexpr std::uint16_t LOW_BYTE_MASK{ 0x00FFU };
    constexpr std::byte BROADCAST_MAC_BYTE{ 0xFFU };

    constexpr auto LINK_ATTEMPTS{ 50U };
    constexpr auto LINK_POLL_INTERVAL{ 100ms };
    constexpr auto FRAME_TIMEOUT{ 100ms };
    constexpr auto CYCLE_INTERVAL{ 500ms };
    constexpr auto FAILURE_BLINK_INTERVAL{ 100ms };

    auto debug(std::string_view message) -> void
    {
        static_cast<void>(std::fwrite(message.data(), sizeof(char), message.size(), stdout));
        static_cast<void>(std::putchar('\n'));
        static_cast<void>(std::fflush(stdout));
    }

    template<typename... Arguments>
        requires(sizeof...(Arguments) > 0U)
    auto debug(const char* pattern, Arguments... arguments) -> void
    {
        // This is the single type-unsafe boundary for the lightweight debug
        // output. Keeping printf here avoids linking the full std::format engine.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        static_cast<void>(std::printf(pattern, arguments...));
        static_cast<void>(std::putchar('\n'));
        static_cast<void>(std::fflush(stdout));
    }

    [[nodiscard]] constexpr auto duplex_name(hal::IEthernet::Duplex duplex) noexcept -> const char*
    {
        switch (duplex) {
            using enum hal::IEthernet::Duplex;
            case Half:
                return "half";
            case Full:
                return "full";
            case Unknown:
                return "unknown";
        }
        return "unknown";
    }

    auto write_little_endian(std::span<std::byte> bytes, std::size_t offset, std::uint16_t value) noexcept
      -> void
    {
        bytes[offset] = static_cast<std::byte>(value & LOW_BYTE_MASK);
        bytes[offset + 1U] = static_cast<std::byte>(value >> hal::IEthernet::BITS_PER_BYTE);
    }

    auto write_big_endian(std::span<std::byte> bytes,
                          std::size_t offset,
                          hal::IEthernet::EtherType value) noexcept -> void
    {
        bytes[offset] = static_cast<std::byte>(std::to_underlying(value) >> hal::IEthernet::BITS_PER_BYTE);
        bytes[offset + 1U] = static_cast<std::byte>(std::to_underlying(value) & LOW_BYTE_MASK);
    }

    [[nodiscard]] auto read_little_endian(std::span<const std::byte> bytes, std::size_t offset) noexcept
      -> std::uint16_t
    {
        return static_cast<std::uint16_t>(
          std::to_integer<std::uint8_t>(bytes[offset]) |
          static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U]))
            << hal::IEthernet::BITS_PER_BYTE);
    }

    [[nodiscard]] auto make_probe(std::uint8_t index) -> std::array<std::byte, hal::IEthernet::MIN_FRAME_SIZE>
    {
        std::array<std::byte, hal::IEthernet::MIN_FRAME_SIZE> frame{};
        for (std::size_t byte{}; byte < hal::IEthernet::MAC_ADDRESS_SIZE; ++byte) {
            frame[DESTINATION_OFFSET + byte] = BROADCAST_MAC_BYTE;
        }

        write_big_endian(frame, hal::IEthernet::ETHER_TYPE_OFFSET, hal::IEthernet::EtherType::EtherCAT);
        write_little_endian(frame, ETHERCAT_HEADER_OFFSET, ETHERCAT_PROTOCOL_HEADER);
        frame[COMMAND_OFFSET] = static_cast<std::byte>(BROADCAST_READ_COMMAND);
        frame[INDEX_OFFSET] = static_cast<std::byte>(index);
        write_little_endian(frame, ADDRESS_POSITION_OFFSET, 0U);
        write_little_endian(frame, ADDRESS_REGISTER_OFFSET, AL_STATUS_REGISTER);
        write_little_endian(frame, DATAGRAM_LENGTH_OFFSET, AL_STATUS_SIZE);
        write_little_endian(frame, IRQ_OFFSET, 0U);
        write_little_endian(frame, DATA_OFFSET, 0U);
        write_little_endian(frame, WORKING_COUNTER_OFFSET, 0U);
        return frame;
    }

    [[nodiscard]] auto valid_response(std::span<const std::byte> frame,
                                      std::uint8_t index,
                                      const hal::IEthernet::MacAddress& local_mac) noexcept -> bool
    {
        if (frame.size() < RESPONSE_SIZE ||
            hal::IEthernet::frameEtherType(frame) != hal::IEthernet::EtherType::EtherCAT ||
            read_little_endian(frame, ETHERCAT_HEADER_OFFSET) != ETHERCAT_PROTOCOL_HEADER ||
            std::to_integer<std::uint8_t>(frame[COMMAND_OFFSET]) != BROADCAST_READ_COMMAND ||
            std::to_integer<std::uint8_t>(frame[INDEX_OFFSET]) != index ||
            read_little_endian(frame, ADDRESS_REGISTER_OFFSET) != AL_STATUS_REGISTER ||
            (read_little_endian(frame, DATAGRAM_LENGTH_OFFSET) & DATAGRAM_LENGTH_MASK) != AL_STATUS_SIZE) {
            return false;
        }

        for (std::size_t byte{}; byte < hal::IEthernet::MAC_ADDRESS_SIZE; ++byte) {
            if (frame[DESTINATION_OFFSET + byte] != BROADCAST_MAC_BYTE ||
                frame[SOURCE_OFFSET + byte] != static_cast<std::byte>(local_mac[byte])) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto wait_for_link(const hal::IEthernet& ethernet) -> bool
    {
        debug("[ethercat] waiting up to %zu ms for link",
              static_cast<std::size_t>(LINK_ATTEMPTS) * static_cast<std::size_t>(LINK_POLL_INTERVAL.count()));
        for (unsigned int attempt{}; attempt < LINK_ATTEMPTS; ++attempt) {
            const auto link{ ethernet.getLinkInfo() };
            if (link && link->up) {
                debug("[ethercat] link up: %lu Mbit/s, %s duplex",
                      static_cast<unsigned long>(link->speed_mbps),
                      duplex_name(link->duplex));
                return true;
            }
            std::this_thread::sleep_for(LINK_POLL_INTERVAL);
        }
        debug("[ethercat] link did not come up");
        return false;
    }

    [[nodiscard]] auto exchange_probe(hal::IEthernet& ethernet, std::uint8_t index) -> bool
    {
        const auto probe{ make_probe(index) };
        const auto transmitted{ ethernet.transmit(probe, FRAME_TIMEOUT) };
        if (!transmitted) {
            debug("[ethercat] TX failed: error %d", transmitted.error().value());
            return false;
        }
        debug("[ethercat] TX index=%u bytes=%zu", static_cast<unsigned int>(index), probe.size());

        std::array<std::byte, hal::IEthernet::MAX_FRAME_SIZE> response{};
        const auto received{ ethernet.receive(response, FRAME_TIMEOUT) };
        if (!received) {
            debug("[ethercat] RX failed: error %d", received.error().value());
            return false;
        }

        const std::span<const std::byte> frame{ response.data(), *received };
        if (!valid_response(frame, index, ethernet.getMacAddress())) {
            debug("[ethercat] RX rejected: invalid frame, index=%u, bytes=%zu",
                  static_cast<unsigned int>(index),
                  *received);
            return false;
        }

        const std::uint16_t working_counter{ read_little_endian(frame, WORKING_COUNTER_OFFSET) };
        const std::uint16_t slave_count{ read_little_endian(frame, ADDRESS_POSITION_OFFSET) };
        const std::uint16_t al_status{ read_little_endian(frame, DATA_OFFSET) };
        debug("[ethercat] RX index=%u bytes=%zu slaves=%u WKC=%u AL status=0x%04X",
              static_cast<unsigned int>(index),
              *received,
              static_cast<unsigned int>(slave_count),
              static_cast<unsigned int>(working_counter),
              static_cast<unsigned int>(al_status));
        if (working_counter == 0U) {
            debug("[ethercat] RX rejected: no EtherCAT slave processed the datagram");
            return false;
        }
        // BRD increments ADP once at every slave. Every ESC supports AL Status,
        // so the returned ADP and working counter must identify the same number
        // of slaves.
        if (slave_count != working_counter) {
            debug("[ethercat] RX rejected: slave count %u does not match WKC %u",
                  static_cast<unsigned int>(slave_count),
                  static_cast<unsigned int>(working_counter));
            return false;
        }
        return true;
    }

    [[noreturn]] auto indicate_failure() -> void
    {
        const auto red_led{ hal::board::createLed(hal::board::LedId::Red) };
        if (red_led == nullptr) {
            Error_Handler();
        }
        debug("[ethercat] TEST FAILED - red LED indicates failure");
        while (true) {
            red_led->toggle();
            std::this_thread::sleep_for(FAILURE_BLINK_INTERVAL);
        }
    }
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
    hal::IEthernet::Configuration configuration{};
#if defined(HAL_PLATFORM_LINUX)
    if (argc > 1 && argv[1] != nullptr) {
        configuration.interface_name = argv[1];
    }
    debug("[ethercat] raw-frame test starting on interface '%.*s'",
          static_cast<int>(configuration.interface_name.size()),
          configuration.interface_name.data());
#else
    debug("[ethercat] raw-frame test starting on STM32 Ethernet peripheral");
#endif

    const auto ethernet{ hal::ethernet::create(configuration) };
    if (!ethernet) {
        debug("[ethercat] driver creation failed");
        indicate_failure();
    }

    const auto started{ ethernet->start() };
    if (!started) {
        debug("[ethercat] driver start failed: error %d", started.error().value());
        indicate_failure();
    }
    if (!wait_for_link(*ethernet)) {
        indicate_failure();
    }

    debug("[ethercat] sending BRD probes for AL Status register 0x%04X",
          static_cast<unsigned int>(AL_STATUS_REGISTER));
    const auto green_led{ hal::board::createLed(hal::board::LedId::Green) };
    if (green_led == nullptr) {
        debug("[ethercat] green LED creation failed");
        indicate_failure();
    }
    std::uint8_t datagram_index{};
    while (exchange_probe(*ethernet, datagram_index++)) {
        green_led->toggle();
        std::this_thread::sleep_for(CYCLE_INTERVAL);
    }
    indicate_failure();
}
