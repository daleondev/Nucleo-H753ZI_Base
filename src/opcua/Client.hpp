#ifndef OPCUA_CLIENT_HPP
#define OPCUA_CLIENT_HPP

#include <tx_api.h>

#include <array>
#include <atomic>
#include <cstddef>

struct UA_Client;

namespace opcua
{
    /*
     * Owns the lifecycle of an open62541 UA_Client and the ThreadX thread that
     * connects it and drives its iterate loop. After start() succeeds, callers
     * may use handle() to perform raw open62541 API calls (read, write,
     * browse, subscriptions, etc.).
     *
     * NOTE: open62541's UA_Client API is not thread-safe; callers must not
     * invoke the raw API concurrently with the run loop without their own
     * synchronisation.
     */
    class Client
    {
      public:
        Client() = default;
        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;
        ~Client();

        // endpointUrl: e.g. "opc.tcp://10.10.10.2:4840". Must outlive the call.
        bool start(const char* endpointUrl);
        void stop();

        UA_Client* handle() const noexcept { return m_client; }
        bool isConnected() const noexcept { return m_connected.load(std::memory_order_acquire); }

      private:
        static void txEntry(ULONG argument);
        void run();

        UA_Client* m_client{ nullptr };
        const char* m_endpointUrl{ nullptr };
        TX_THREAD m_thread{};
        alignas(8) std::array<std::byte, 16384> m_stack{};
        std::atomic<bool> m_running{ false };
        std::atomic<bool> m_connected{ false };
    };
} // namespace opcua

#endif // OPCUA_CLIENT_HPP
