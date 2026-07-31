# Distributed Key-Value Store with Consistent-Hashing Load Balancer

A production-inspired distributed key-value store built from scratch in C++, featuring a custom TCP reverse proxy with consistent hashing, replication, health monitoring, and automatic node recovery — all without any distributed systems framework.

---

## Architecture Overview

```
┌────────────┐        ┌──────────────────────────────────────┐
│  kvclient  │──HTTP──▶         Load Balancer (:8080)         │
└────────────┘        │                                        │
                      │  ┌─────────┐  ┌──────────────────┐   │
                      │  │ Router  │  │  HealthChecker   │   │
                      │  │(HashRing│  │  (background     │   │
                      │  │ + state)│  │   thread, 3s)    │   │
                      │  └────┬────┘  └────────┬─────────┘   │
                      │       │                │              │
                      │  ┌────▼────────────────▼──────────┐  │
                      │  │        ProxySession             │  │
                      │  │  (per-client worker thread)     │  │
                      │  └────────────────────────────────┘  │
                      └──────────────────────────────────────┘
                               │               │
               ┌───────────────┼───────────────┐
               ▼               ▼               ▼
        KVServer(:9001)  KVServer(:9002)  KVServer(:9003)
        PostgreSQL DB    PostgreSQL DB    PostgreSQL DB
        LRU Cache        LRU Cache        LRU Cache
```

---

## Components

### Load Balancer (`LoadBalancer`, `main.cpp`)
- Custom TCP server built on raw POSIX sockets (`socket`, `bind`, `listen`, `accept`)
- Spawns a detached `std::thread` per client connection for concurrent request handling
- Bootstraps the `Router`, `RecoveryManager`, and `HealthChecker` and wires their lifetimes together
- Configures `SO_REUSEADDR` for zero-downtime restarts

### Consistent Hash Ring (`HashRing`)
- Implements a **virtual-node consistent hash ring** using `std::map<uint32_t, Backend>` as the sorted ring structure
- Each backend is mapped to **50 virtual nodes** (configurable) to ensure balanced key distribution even with few physical nodes
- `add_node` / `remove_node` insert/erase all virtual node entries; ring lookup uses `lower_bound` for O(log n) successor search
- `getNodes(key, replication_factor)` returns N distinct physical backends clockwise from the key's hash position — used for both replication targeting and recovery ownership checks
- Thread-safe via `std::mutex`

### Router (`Router`)
- Owns the `HashRing` and a `backends_` map keyed by port, tracking each node's `BackendStatus` (`UP` / `RECOVERING` / `DOWN`)
- **Read path:** only routes to `UP` backends — prevents stale reads from nodes still catching up
- **Write path:** routes to both `UP` and `RECOVERING` backends — ensures a recovering node receives live writes immediately on rejoining the ring
- `markBackendDown` removes the node from the hash ring instantly, so subsequent key lookups naturally route to its successor
- `markBackendRecovering` re-adds the node to the ring before data sync begins, accepting new writes while historical data is being replayed

### Proxy Session (`ProxySession`)
- Implements the full HTTP/1.1 proxying loop: reads from the client socket, invokes `parseHTTPRequest`, routes via `Router`, forwards to backend(s), reads the backend response with `readBackendResponse`, and relays it back
- **Write replication:** for `POST`/`PUT`/`DELETE`, connects sequentially to all replica backends; injects `X-Internal-Replication: true` header on `PUT` so backend servers can upsert instead of update-only
- **Read failover:** tries replica backends in ring order; on any failure, marks the backend DOWN and tries the next successor
- Handles persistent client connections (outer recv loop) with pipelined request parsing (inner parse loop consuming `clientBuffer` by `result.consumed` bytes per request)

### HTTP Parser (`HttpHelper`, `HttpParser.h`)
- Hand-written incremental HTTP/1.1 request and response parser — no external HTTP library
- Request parser: extracts method, path, version; reads `Content-Length` via `std::from_chars` for zero-copy integer parsing; returns `INCOMPLETE` until the full body has arrived, enabling correct handling of pipelined and partial reads
- Response parser: handles status line, headers map, and body; falls back to consuming remaining buffer bytes when `Content-Length` is absent

### Health Checker (`HealthChecker`)
- Runs a dedicated `std::thread` pinging every backend's `/health` endpoint every **3 seconds** using `cpp-httplib`
- Drives the `DOWN → RECOVERING → UP` state machine: on revival, triggers `RecoveryManager::recover` synchronously before promoting the node to `UP`, preventing dirty reads
- Uses `std::atomic<bool> running_` for clean thread shutdown

### Recovery Manager (`RecoveryManager`)
- On node revival, scans **all online peers** to build a global deduplicated keyspace (`std::set<std::string>`)
- For each key in the global keyspace, recomputes its replica set via `router_.getBackendsForKey`; only syncs keys the recovered node is supposed to own
- Fetches values from a live peer replica via `GET /kvstore/<key>` and pushes them to the recovered node via `POST /kvstore/sync/<key>` — a safe upsert route that never overwrites a newer value

### KV Server (`kvserver`)
- HTTP REST server built on `cpp-httplib` with a **300-thread pool**
- **API:** `POST /kvstore/create`, `GET /kvstore/<key>`, `PUT /kvstore/<key>`, `DELETE /kvstore/<key>`, `GET /kvstore/allkeys`, `POST /kvstore/sync/<key>`, `GET /health`
- Persistence via **PostgreSQL** (libpqxx); each server instance connects to its own database (`kvstore_<port>`) for isolation
- **LRU Cache** sits in front of every read: cache hit returns immediately without a DB round-trip; writes invalidate the cache entry; implemented with `std::list` + `std::unordered_map` for O(1) get/put/evict, protected by `std::mutex`
- Replication-aware `PUT`: detects `X-Internal-Replication` header and performs an `INSERT ... ON CONFLICT DO UPDATE` (upsert) instead of a plain `UPDATE`, so a recovering node can accept writes for keys it doesn't yet have

### Load Generator (`load_gen`)
- Multi-threaded benchmarking tool; configurable thread count, duration, and workload type
- Workload modes: `put_all` (pure write), `get_all` (pure read), `get_popular` (hot-key / cache-hit stress), `get_put` (50/50 mixed)
- Reports throughput (req/s) and average latency (ms); appends results to `results.csv` for offline analysis

---

## Key Design Decisions

| Decision | Rationale |
|---|---|
| Virtual nodes on hash ring | Prevents hot spots when node count is small; each physical node covers multiple non-contiguous ring segments |
| Separate read/write routing with status gating | Avoids stale reads from a recovering node while still funneling live writes to it immediately |
| `X-Internal-Replication` header | Lets a single `PUT` endpoint behave correctly for both normal client updates (key must exist) and replication upserts (key may not exist yet) |
| Recovery via `/kvstore/sync` | Idempotent sync route decoupled from the client-facing `PUT`; safe to replay without risk of overwriting newer client data |
| Hand-written HTTP parser | Avoids a full HTTP library dependency in the proxy hot path; incremental design handles partial TCP reads and pipelined requests correctly |
| Per-client threads + detach | Simple concurrency model; each `ProxySession` is fully independent with its own buffer and backend socket state |
| LRU cache per server | Absorbs repeated reads for hot keys without hitting PostgreSQL; invalidated on write to prevent stale cache serving |

---

## Building & Running

### Prerequisites
- C++17 compiler (g++ / clang++)
- PostgreSQL + libpqxx
- [cpp-httplib](https://github.com/yhirose/cpp-httplib) (header-only, place `httplib.h` in project root)

### Database Setup
```bash
# Create one database per server instance
createdb kvstore_9001
createdb kvstore_9002
createdb kvstore_9003

# In each database
psql -d kvstore_900X -c "
  CREATE TABLE kv_data (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
  );"
```

### Compile
```bash
# KV Servers
g++ -std=c++17 -O2 -o kvserver kvserver.cpp -lpqxx -lpq -lpthread

# Load Balancer
g++ -std=c++17 -O2 -o loadbalancer \
    main.cpp LoadBalancer.cpp Router.cpp HashRing.cpp \
    ProxySession.cpp HttpHelper.cpp HealthChecker.cpp RecoveryManager.cpp \
    -lpthread

# Load Generator
g++ -std=c++17 -O2 -o load_gen load_gen.cpp -lpthread

# Client
g++ -std=c++17 -O2 -o kvclient kvclient.cpp
```

### Run
```bash
# Start three server instances
./kvserver 9001 &
./kvserver 9002 &
./kvserver 9003 &

# Start load balancer (connects to all three)
./loadbalancer

# Interact via client
./kvclient
>> create foo bar
>> read foo
>> update foo baz
>> delete foo
```

### Benchmarking
```bash
./load_gen
# Enter threads, duration, workload type
# Results appended to results.csv
```

---

## Fault Tolerance Walkthrough

1. **Node goes down** — HealthChecker detects missed `/health` ping → calls `router_.markBackendDown(port)` → node removed from hash ring → all subsequent key lookups route to ring successors automatically
2. **Node comes back** — HealthChecker detects successful ping → `markBackendRecovering` re-adds node to ring (receives live writes) → `RecoveryManager::recover` replays all owned keys from peers → `markBackendUp` opens node for reads
3. **Write during recovery** — write is replicated to the recovering node via the upsert path; no key is lost
4. **All replicas for a key are down** — ProxySession returns `HTTP 503` to the client

---

## Project Structure

```
├── main.cpp               # Entry point
├── LoadBalancer.{h,cpp}   # TCP server, thread spawning
├── Router.{h,cpp}         # Key routing, backend state management
├── HashRing.{h,cpp}       # Consistent hash ring with virtual nodes
├── ProxySession.{h,cpp}   # Per-client HTTP proxy and replication logic
├── HttpParser.h           # HTTP request/response structs
├── HttpHelper.cpp         # Incremental HTTP/1.1 parser
├── HealthChecker.{h,cpp}  # Background health ping loop
├── RecoveryManager.{h,cpp}# Node recovery and key sync
├── Backend.h              # Backend struct and status enum
├── kvserver.cpp           # KV HTTP server (cpp-httplib + libpqxx)
├── kvcache.h              # Thread-safe LRU cache
├── kvclient.cpp           # Interactive CLI client
└── load_gen.cpp           # Multi-threaded load generator
```