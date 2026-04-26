#ifndef OPCUA_BENCH_FIXTURE_HPP
#define OPCUA_BENCH_FIXTURE_HPP

#include <cstdint>

struct UA_Server;

namespace opcua
{
    /*
     * Numeric NodeIds for the benchmark fixture nodes. Kept as constants so
     * the client side can refer to them without a runtime browse/translate.
     * All live in namespace 1 (the application namespace created by
     * UA_ServerConfig_setMinimal).
     */
    constexpr std::uint16_t BENCH_NAMESPACE_INDEX{ 1 };
    constexpr std::uint32_t BENCH_NODEID_SCALAR_U64{ 5001 };
    constexpr std::uint32_t BENCH_NODEID_SCALAR_DOUBLE{ 5002 };
    constexpr std::uint32_t BENCH_NODEID_ECHO_U64{ 5003 };

    /*
     * A pool of writable UInt64 nodes used by the monitored-item benchmark.
     * NodeIds are BENCH_NODEID_MON_BASE + index, with index in
     * [0, BENCH_MON_NODE_COUNT). The bench creates a subscription with N of
     * these as monitored items, writes a sequence number to one of them,
     * and times the round-trip until the corresponding data-change
     * notification arrives at the client callback.
     */
    constexpr std::uint32_t BENCH_NODEID_MON_BASE{ 6000 };
    constexpr std::uint32_t BENCH_MON_NODE_COUNT{ 32 };

    // Add the benchmark fixture nodes (scalar UInt64, scalar Double, echo
    // UInt64) under Objects. Returns true on success. Must be called after
    // server.start() succeeds and before the server iterate loop is heavily
    // loaded; the open62541 API is not thread-safe vs. the run loop.
    bool populateBenchFixture(UA_Server* server);
} // namespace opcua

#endif // OPCUA_BENCH_FIXTURE_HPP
