#include "BenchFixture.hpp"

#include <open62541/server.h>
#include <open62541/types.h>

#include <cstdio>

namespace opcua
{
    namespace
    {
        bool addScalar(UA_Server* server,
                       std::uint32_t numericNodeId,
                       const char* browseName,
                       const UA_DataType& type,
                       const void* initialValue)
        {
            UA_VariableAttributes attr = UA_VariableAttributes_default;
            UA_Variant_setScalar(&attr.value, const_cast<void*>(initialValue), &type);
            attr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", browseName);
            attr.dataType = type.typeId;
            attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

            UA_NodeId nodeId = UA_NODEID_NUMERIC(BENCH_NAMESPACE_INDEX, numericNodeId);
            UA_QualifiedName qn = UA_QUALIFIEDNAME_ALLOC(BENCH_NAMESPACE_INDEX, browseName);
            const UA_StatusCode rc =
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
            if (rc != UA_STATUSCODE_GOOD) {
                std::printf("bench: failed to add node %s (0x%08x)\n", browseName, static_cast<unsigned>(rc));
                std::fflush(stdout);
                return false;
            }
            return true;
        }
    } // namespace

    bool populateBenchFixture(UA_Server* server)
    {
        if (server == nullptr) {
            return false;
        }
        UA_UInt64 zeroU64 = 0;
        UA_Double zeroD = 0.0;
        bool ok = true;
        ok =
          ok &&
          addScalar(server, BENCH_NODEID_SCALAR_U64, "bench.scalar_u64", UA_TYPES[UA_TYPES_UINT64], &zeroU64);
        ok = ok &&
             addScalar(
               server, BENCH_NODEID_SCALAR_DOUBLE, "bench.scalar_double", UA_TYPES[UA_TYPES_DOUBLE], &zeroD);
        ok = ok &&
             addScalar(server, BENCH_NODEID_ECHO_U64, "bench.echo_u64", UA_TYPES[UA_TYPES_UINT64], &zeroU64);

        for (std::uint32_t i = 0; i < BENCH_MON_NODE_COUNT && ok; ++i) {
            char browseName[32];
            std::snprintf(browseName, sizeof(browseName), "bench.mon_u64.%u", static_cast<unsigned>(i));
            ok = ok && addScalar(
                         server, BENCH_NODEID_MON_BASE + i, browseName, UA_TYPES[UA_TYPES_UINT64], &zeroU64);
        }
        return ok;
    }
} // namespace opcua
