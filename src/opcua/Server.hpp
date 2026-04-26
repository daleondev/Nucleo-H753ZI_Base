#ifndef OPCUA_SERVER_HPP
#define OPCUA_SERVER_HPP

#include <tx_api.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

struct UA_Server;

namespace opcua
{
    /*
     * Owns the lifecycle of an open62541 UA_Server and the ThreadX thread that
     * drives its run loop. After start() succeeds, callers may use handle() to
     * perform raw open62541 API calls (add nodes, write values, etc.).
     *
     * NOTE: open62541's UA_Server API is not thread-safe; callers must not
     * invoke the raw API concurrently with the run loop without their own
     * synchronisation.
     */
    class Server
    {
      public:
        Server() = default;
        Server(const Server&) = delete;
        Server& operator=(const Server&) = delete;
        ~Server();

        // host: dotted-IP or DNS name embedded into discovery/server URLs
        //   (e.g. "10.10.10.2"). Must outlive the call.
        bool start(std::uint16_t portNumber, const char* host);
        void stop();

        UA_Server* handle() const noexcept { return m_server; }

      private:
        static void txEntry(ULONG argument);
        void run();

        UA_Server* m_server{ nullptr };
        TX_THREAD m_thread{};
        alignas(8) std::array<std::byte, 16384> m_stack{};
        std::atomic<bool> m_running{ false };
    };
} // namespace opcua

#endif // OPCUA_SERVER_HPP
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

        // host: dotted-IP or DNS name embedded into discovery/server URLs
        //   (e.g. "10.10.10.2" or "stm32-nucleo"). Must outlive the call.
        bool start(std::uint16_t portNumber, const char* host);
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
