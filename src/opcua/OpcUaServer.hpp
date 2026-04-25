#ifndef OPCUA_OPCUASERVER_HPP
#define OPCUA_OPCUASERVER_HPP

#include <tx_api.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

struct UA_Server;

namespace opcua
{
    class OpcUaServer
    {
      public:
        OpcUaServer() = default;
        OpcUaServer(const OpcUaServer&) = delete;
        OpcUaServer& operator=(const OpcUaServer&) = delete;
        ~OpcUaServer();

        bool start(std::uint16_t portNumber);
        void stop();

      private:
        static void txEntry(ULONG argument);
        void run();

        UA_Server* m_server{ nullptr };
        TX_THREAD m_thread{};
        alignas(8) std::array<std::byte, 16384> m_stack{};
        std::atomic<bool> m_running{ false };
        std::atomic<std::uint32_t> m_counter{ 0 };
    };
} // namespace opcua

#endif // OPCUA_OPCUASERVER_HPP
