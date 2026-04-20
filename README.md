# ASNPrefixMap

High-performance C++20 tool for building ASN → Prefix mappings from live BGP streams (RIS Live).

> Builds a near-complete global routing view in ~30–40 minutes using real-time data.

---

## Dataset Convergence

![Prefix growth](docs/prefix_plateau.png)
![ASN growth](docs/asn_growth.png)

The system ingests live BGP updates and tracks how quickly new routing information is discovered.

- Rapid growth phase as new data arrives
- Gradual slowdown
- Plateau when most globally visible routes are observed

Plateau detection is automatic and based on rolling averages of prefix discovery rate.

---

## Example Output

```text
[plateau] uptime_sec=2163.51 uptime_hms=00:36:03
[plateau] active_prefixes=1293782 total_unique_asns_ever_seen=84951
[plateau] note=table appears broadly complete; you may stop if a stable mapping is enough

raw_messages_received=26661999
parsed_events_total=69416421
announces_applied=62868720
withdraws_applied=6547701

runtime_sec=5697.49
runtime_hms=01:34:57
plateau_detected=true
plateau_uptime_sec=2163.51
plateau_uptime_hms=00:36:03
```

## Key Features

Real-time ingestion via RIS Live WebSocket

Efficient in-memory routing state using binary prefixes and PeerId

Snapshot persistence in a human-readable format

Plateau detection for dataset convergence

Automatic reconnect on transient network failures

Growth statistics for ASN and prefix discovery rates

## Architecture
```text
RIS Live WebSocket
        ↓
    JSON Parser
        ↓
     BgpEvent
        ↓
  PeerRegistry (PeerId)
        ↓
 RoutingState (binary prefixes)
        ↓
 Snapshot / Export
```
## Performance Design

No string allocations in the hot ingest path

Binary prefix representation for both IPv4 and IPv6

small_vector optimization for per-prefix observations

No exception-based prefix family detection in the hot path

Cache-friendly routing-state layout

## Plateau Detection

Plateau is detected using a rolling average of prefix discovery rate.

When the rate drops below a threshold for a sustained period, the system reports that the routing table is broadly complete.

Plateau detection is heuristic and does not guarantee full completeness.

## Usage
```text
mkdir build
cd build
cmake ..
make
./asn_prefix_map
```

## Future Work

Query tool for snapshot

HTTP API

Docker image
