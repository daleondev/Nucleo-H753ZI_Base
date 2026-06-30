#include "hal/hal.hpp"
#include "hal/drivers/factory/ethernet.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>

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

    auto write_little_endian(std::span<std::byte> bytes,
                             std::size_t offset,
                             std::uint16_t value) noexcept -> void
    {
        bytes[offset] = static_cast<std::byte>(value & LOW_BYTE_MASK);
        bytes[offset + 1U] = static_cast<std::byte>(value >> hal::IEthernet::BITS_PER_BYTE);
    }

    auto write_big_endian(std::span<std::byte> bytes,
                          std::size_t offset,
                          std::uint16_t value) noexcept -> void
    {
        bytes[offset] = static_cast<std::byte>(value >> hal::IEthernet::BITS_PER_BYTE);
        bytes[offset + 1U] = static_cast<std::byte>(value & LOW_BYTE_MASK);
    }

    [[nodiscard]] auto read_little_endian(std::span<const std::byte> bytes,
                                          std::size_t offset) noexcept -> std::uint16_t
    {
        return static_cast<std::uint16_t>(
          std::to_integer<std::uint8_t>(bytes[offset]) |
          static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U]))
            << hal::IEthernet::BITS_PER_BYTE);
    }

    [[nodiscard]] auto make_probe(std::uint8_t index)
      -> std::array<std::byte, hal::IEthernet::MIN_FRAME_SIZE>
    {
        std::array<std::byte, hal::IEthernet::MIN_FRAME_SIZE> frame{};
        for (std::size_t byte{}; byte < hal::IEthernet::MAC_ADDRESS_SIZE; ++byte) {
            frame[DESTINATION_OFFSET + byte] = BROADCAST_MAC_BYTE;
        }

        write_big_endian(
          frame, hal::IEthernet::ETHER_TYPE_OFFSET, hal::IEthernet::ETHERCAT_ETHER_TYPE);
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
            hal::IEthernet::frameEtherType(frame) != hal::IEthernet::ETHERCAT_ETHER_TYPE ||
            read_little_endian(frame, ETHERCAT_HEADER_OFFSET) != ETHERCAT_PROTOCOL_HEADER ||
            std::to_integer<std::uint8_t>(frame[COMMAND_OFFSET]) != BROADCAST_READ_COMMAND ||
            std::to_integer<std::uint8_t>(frame[INDEX_OFFSET]) != index ||
            read_little_endian(frame, ADDRESS_POSITION_OFFSET) != 0U ||
            read_little_endian(frame, ADDRESS_REGISTER_OFFSET) != AL_STATUS_REGISTER ||
            (read_little_endian(frame, DATAGRAM_LENGTH_OFFSET) & DATAGRAM_LENGTH_MASK) !=
              AL_STATUS_SIZE ||
            read_little_endian(frame, WORKING_COUNTER_OFFSET) == 0U) {
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
        for (unsigned int attempt{}; attempt < LINK_ATTEMPTS; ++attempt) {
            const auto link{ ethernet.getLinkInfo() };
            if (link && link->up) {
                return true;
            }
            std::this_thread::sleep_for(LINK_POLL_INTERVAL);
        }
        return false;
    }

    [[nodiscard]] auto exchange_probe(hal::IEthernet& ethernet, std::uint8_t index) -> bool
    {
        const auto probe{ make_probe(index) };
        if (!ethernet.transmit(probe, FRAME_TIMEOUT)) {
            return false;
        }

        std::array<std::byte, hal::IEthernet::MAX_FRAME_SIZE> response{};
        const auto received{ ethernet.receive(response, FRAME_TIMEOUT) };
        return received && valid_response(std::span<const std::byte>{ response.data(), *received },
                                          index,
                                          ethernet.getMacAddress());
    }

    [[noreturn]] auto indicate_failure() -> void
    {
        while (true) {
            if (BSP_LED_Toggle(LED_RED) != BSP_ERROR_NONE) {
                Error_Handler();
            }
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
#endif

    const auto ethernet{ hal::ethernet::create(configuration) };
    if (!ethernet || !ethernet->start() || !wait_for_link(*ethernet)) {
        indicate_failure();
    }

    std::uint8_t datagram_index{};
    while (exchange_probe(*ethernet, datagram_index++)) {
        if (BSP_LED_Toggle(LED_GREEN) != BSP_ERROR_NONE) {
            Error_Handler();
        }
        std::this_thread::sleep_for(CYCLE_INTERVAL);
    }
    indicate_failure();
}
