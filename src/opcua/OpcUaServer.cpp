#include "OpcUaServer.hpp"

#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/types.h>
#include <open62541/util.h>

#include <cstdio>
#include <cstring>

namespace opcua
{
    namespace
    {
        constexpr UINT OPCUA_THREAD_PRIO{ 12 };
        constexpr ULONG OPCUA_RUN_INTERVAL_MS{ 100 };
        constexpr UA_UInt16 DEMO_NAMESPACE{ 1 };

        // ThreadX passes a single ULONG (32-bit on x86_64-linux), too small
        // for a pointer. The application instantiates a single OpcUaServer,
        // so we stash it in a file-scope pointer that the entry trampoline
        // dereferences.
        OpcUaServer* s_instance{ nullptr };

        UA_NodeId addUInt64Variable(UA_Server* server, const char* browseName, std::uint64_t initialValue)
        {
            UA_VariableAttributes attr = UA_VariableAttributes_default;
            UA_UInt64 value = initialValue;
            UA_Variant_setScalar(&attr.value, &value, &UA_TYPES[UA_TYPES_UINT64]);
            attr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", browseName);
            attr.accessLevel = UA_ACCESSLEVELMASK_READ;

            UA_NodeId nodeId = UA_NODEID_STRING_ALLOC(DEMO_NAMESPACE, browseName);
            UA_QualifiedName qn = UA_QUALIFIEDNAME_ALLOC(DEMO_NAMESPACE, browseName);
            UA_Server_addVariableNode(server,
                                      nodeId,
                                      UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
                                      UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                                      qn,
                                      UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
                                      attr,
                                      nullptr,
                                      nullptr);
            UA_QualifiedName_clear(&qn);
            UA_LocalizedText_clear(&attr.displayName);
            return nodeId;
        }
    } // namespace

    OpcUaServer::~OpcUaServer() { stop(); }

    bool OpcUaServer::start(std::uint16_t portNumber, const char* host)
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

        addUInt64Variable(m_server, "Demo.Counter", 0);
        addUInt64Variable(m_server, "Demo.Uptime", 0);

        m_running.store(true, std::memory_order_release);
        s_instance = this;

        UINT status = tx_thread_create(&m_thread,
                                       const_cast<CHAR*>("opcua_srv"),
                                       OpcUaServer::txEntry,
                                       0UL,
                                       m_stack.data(),
                                       static_cast<ULONG>(m_stack.size()),
                                       OPCUA_THREAD_PRIO,
                                       OPCUA_THREAD_PRIO,
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

    void OpcUaServer::stop()
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

    void OpcUaServer::txEntry(ULONG argument)
    {
        (void)argument;
        if (s_instance != nullptr) {
            s_instance->run();
        }
    }

    void OpcUaServer::run()
    {
        if (UA_Server_run_startup(m_server) != UA_STATUSCODE_GOOD) {
            std::printf("opcua: run_startup failed\n");
            std::fflush(stdout);
            return;
        }

        const ULONG sleepTicks = (OPCUA_RUN_INTERVAL_MS * TX_TIMER_TICKS_PER_SECOND + 999UL) / 1000UL;

        while (m_running.load(std::memory_order_acquire)) {
            UA_Server_run_iterate(m_server, false);

            // Update the demo counter so a connected client sees changes.
            const auto next = m_counter.fetch_add(1, std::memory_order_relaxed) + 1;
            UA_Variant value;
            UA_UInt64 counterValue = next;
            UA_Variant_setScalar(&value, &counterValue, &UA_TYPES[UA_TYPES_UINT64]);
            UA_NodeId nodeId = UA_NODEID_STRING(DEMO_NAMESPACE, const_cast<char*>("Demo.Counter"));
            UA_Server_writeValue(m_server, nodeId, value);

            tx_thread_sleep(sleepTicks);
        }
        UA_Server_run_shutdown(m_server);
    }
} // namespace opcua
