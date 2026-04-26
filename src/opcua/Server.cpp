#include "Server.hpp"

#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/types.h>
#include <open62541/util.h>

#include <cstdio>

namespace opcua
{
    namespace
    {
        constexpr UINT SERVER_THREAD_PRIO{ 12 };
        constexpr ULONG SERVER_RUN_INTERVAL_MS{ 100 };

        // ThreadX passes a single ULONG (32-bit on x86_64-linux), too small for
        // a pointer. The application instantiates a single Server, so we stash
        // it in a file-scope pointer that the entry trampoline dereferences.
        Server* s_instance{ nullptr };
    } // namespace

    Server::~Server() { stop(); }

    bool Server::start(std::uint16_t portNumber, const char* host)
    {
        if (m_server != nullptr) {
            return false;
        }
        if (host == nullptr || host[0] == '\0') {
            host = "localhost";
        }
        m_server = UA_Server_new();
        if (m_server == nullptr) {
            return false;
        }
        UA_ServerConfig* config = UA_Server_getConfig(m_server);
        if (UA_ServerConfig_setMinimal(config, portNumber, nullptr) != UA_STATUSCODE_GOOD) {
            UA_Server_delete(m_server);
            m_server = nullptr;
            return false;
        }

#if defined(HAL_COMPAT_STM32)
        // Default 64 kB chunk buffer is far larger than anything we exchange
        // (small scalar reads / writes) and quickly exhausts the heap once a
        // second client connects. 8 kB is plenty and matches the bench client
        // and production client config below.
        config->tcpBufSize = 8192;
#endif

        // Replace the default "opc.tcp://:<port>" with one carrying our host.
        char urlBuf[128];
        std::snprintf(urlBuf, sizeof(urlBuf), "opc.tcp://%s:%u", host, portNumber);
        if (config->serverUrls != nullptr) {
            UA_Array_delete(config->serverUrls, config->serverUrlsSize, &UA_TYPES[UA_TYPES_STRING]);
            config->serverUrls = nullptr;
            config->serverUrlsSize = 0;
        }
        UA_String newUrl = UA_STRING(urlBuf);
        if (UA_Array_copy(
              &newUrl, 1, reinterpret_cast<void**>(&config->serverUrls), &UA_TYPES[UA_TYPES_STRING]) !=
            UA_STATUSCODE_GOOD) {
            UA_Server_delete(m_server);
            m_server = nullptr;
            return false;
        }
        config->serverUrlsSize = 1;

        // Mirror the host into the application URI for nicer discovery output.
        char uriBuf[160];
        std::snprintf(uriBuf, sizeof(uriBuf), "urn:%s:NUCLEO-H753ZI:Application", host);
        UA_String_clear(&config->applicationDescription.applicationUri);
        config->applicationDescription.applicationUri = UA_STRING_ALLOC(uriBuf);

        m_running.store(true, std::memory_order_release);
        s_instance = this;

        UINT status = tx_thread_create(&m_thread,
                                       const_cast<CHAR*>("opcua_srv"),
                                       Server::txEntry,
                                       0UL,
                                       m_stack.data(),
                                       static_cast<ULONG>(m_stack.size()),
                                       SERVER_THREAD_PRIO,
                                       SERVER_THREAD_PRIO,
                                       TX_NO_TIME_SLICE,
                                       TX_AUTO_START);
        if (status != TX_SUCCESS) {
            m_running.store(false, std::memory_order_release);
            UA_Server_delete(m_server);
            m_server = nullptr;
            return false;
        }
        std::printf("opcua: server listening on opc.tcp://%s:%u\n", host, portNumber);
        std::fflush(stdout);
        return true;
    }

    void Server::stop()
    {
        if (!m_running.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        // Allow the run loop to observe the flag and exit.
        tx_thread_sleep(static_cast<ULONG>(2));
        if (m_server != nullptr) {
            UA_Server_delete(m_server);
            m_server = nullptr;
        }
    }

    void Server::txEntry(ULONG argument)
    {
        (void)argument;
        if (s_instance != nullptr) {
            s_instance->run();
        }
    }

    void Server::run()
    {
        if (UA_Server_run_startup(m_server) != UA_STATUSCODE_GOOD) {
            std::printf("opcua: server run_startup failed\n");
            std::fflush(stdout);
            return;
        }

        const ULONG sleepTicks = (SERVER_RUN_INTERVAL_MS * TX_TIMER_TICKS_PER_SECOND + 999UL) / 1000UL;

        while (m_running.load(std::memory_order_acquire)) {
            UA_Server_run_iterate(m_server, false);
            tx_thread_sleep(sleepTicks);
        }
        UA_Server_run_shutdown(m_server);
    }
} // namespace opcua
