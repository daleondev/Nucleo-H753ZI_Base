#include "Client.hpp"

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/types.h>

#include <cstdio>

namespace opcua
{
    namespace
    {
        constexpr UINT CLIENT_THREAD_PRIO{ 13 };
        constexpr ULONG CLIENT_RUN_INTERVAL_MS{ 100 };
        constexpr ULONG CLIENT_CONNECT_RETRY_MS{ 1000 };

        // ThreadX passes a single ULONG (32-bit on x86_64-linux), too small for
        // a pointer. The application instantiates a single Client, so we stash
        // it in a file-scope pointer that the entry trampoline dereferences.
        Client* s_instance{ nullptr };
    } // namespace

    Client::~Client() { stop(); }

    bool Client::start(const char* endpointUrl)
    {
        if (m_client != nullptr || endpointUrl == nullptr || endpointUrl[0] == '\0') {
            return false;
        }
        m_client = UA_Client_new();
        if (m_client == nullptr) {
            return false;
        }
        if (UA_ClientConfig_setDefault(UA_Client_getConfig(m_client)) != UA_STATUSCODE_GOOD) {
            UA_Client_delete(m_client);
            m_client = nullptr;
            return false;
        }

        m_endpointUrl = endpointUrl;
        m_running.store(true, std::memory_order_release);
        m_connected.store(false, std::memory_order_release);
        s_instance = this;

        UINT status = tx_thread_create(&m_thread,
                                       const_cast<CHAR*>("opcua_cli"),
                                       Client::txEntry,
                                       0UL,
                                       m_stack.data(),
                                       static_cast<ULONG>(m_stack.size()),
                                       CLIENT_THREAD_PRIO,
                                       CLIENT_THREAD_PRIO,
                                       TX_NO_TIME_SLICE,
                                       TX_AUTO_START);
        if (status != TX_SUCCESS) {
            m_running.store(false, std::memory_order_release);
            UA_Client_delete(m_client);
            m_client = nullptr;
            return false;
        }
        std::printf("opcua: client targeting %s\n", endpointUrl);
        std::fflush(stdout);
        return true;
    }

    void Client::stop()
    {
        if (!m_running.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        // Allow the run loop to observe the flag and exit.
        tx_thread_sleep(static_cast<ULONG>(2));
        if (m_client != nullptr) {
            UA_Client_disconnect(m_client);
            UA_Client_delete(m_client);
            m_client = nullptr;
        }
        m_connected.store(false, std::memory_order_release);
    }

    void Client::txEntry(ULONG argument)
    {
        (void)argument;
        if (s_instance != nullptr) {
            s_instance->run();
        }
    }

    void Client::run()
    {
        const ULONG iterateTicks = (CLIENT_RUN_INTERVAL_MS * TX_TIMER_TICKS_PER_SECOND + 999UL) / 1000UL;
        const ULONG retryTicks = (CLIENT_CONNECT_RETRY_MS * TX_TIMER_TICKS_PER_SECOND + 999UL) / 1000UL;

        // Connect with retry; the server thread may still be opening its
        // listen socket when we get here.
        while (m_running.load(std::memory_order_acquire)) {
            UA_StatusCode rc = UA_Client_connect(m_client, m_endpointUrl);
            if (rc == UA_STATUSCODE_GOOD) {
                std::printf("opcua: client connected to %s\n", m_endpointUrl);
                std::fflush(stdout);
                m_connected.store(true, std::memory_order_release);
                break;
            }
            std::printf("opcua: client connect failed (0x%08x), retrying...\n", static_cast<unsigned>(rc));
            std::fflush(stdout);
            tx_thread_sleep(retryTicks);
        }

        while (m_running.load(std::memory_order_acquire)) {
            UA_StatusCode rc = UA_Client_run_iterate(m_client, 0);
            if (rc != UA_STATUSCODE_GOOD) {
                m_connected.store(false, std::memory_order_release);
                std::printf("opcua: client iterate returned 0x%08x, reconnecting...\n",
                            static_cast<unsigned>(rc));
                std::fflush(stdout);
                UA_Client_disconnect(m_client);
                while (m_running.load(std::memory_order_acquire)) {
                    if (UA_Client_connect(m_client, m_endpointUrl) == UA_STATUSCODE_GOOD) {
                        m_connected.store(true, std::memory_order_release);
                        break;
                    }
                    tx_thread_sleep(retryTicks);
                }
            }
            tx_thread_sleep(iterateTicks);
        }

        UA_Client_disconnect(m_client);
    }
} // namespace opcua
