# Architecture

ASNPrefixMap builds a practical ASN ↔ prefix mapping from live BGP updates received from RIS Live.

The system is intentionally **not** a full BGP router.  
Instead, it maintains a compact routing-state model suitable for prefix-to-origin-ASN mapping, snapshot persistence, and export.

## High-Level Data Flow

```text
RIS Live WebSocket
        ↓
Raw JSON messages
        ↓
RIS Live parser
        ↓
BgpEvent
        ↓
PeerRegistry
        ↓
RoutingState
        ↓
Snapshot / Export / Growth Stats
```

## Core Components
### RIS Live WebSocket Source

Responsible for:

* connecting to RIS Live over TLS/WebSocket

* sending subscription requests

* reading raw JSON messages

* reconnecting after transient network failures

This layer does not implement BGP logic. It only provides raw message transport.

### RIS Live Parser

Converts raw RIS Live JSON messages into normalized BgpEvent objects.

A single incoming message may produce:

* zero events

* one event

* multiple announce/withdraw events

### PeerRegistry

Peer identity is normalized into a compact PeerId.

Instead of storing peer strings repeatedly in the routing state, ASNPrefixMap stores:

* PeerId in the hot path

* full PeerInfo separately

This reduces memory usage and avoids repeated string-heavy keys.

### RoutingState

RoutingState maintains active observations per prefix.

Key properties:

* binary prefix representation for IPv4 and IPv6

* separate runtime storage for IPv4 and IPv6 prefixes

* compact per-prefix observation storage

* exported ASN selection based on peer observations

The project uses a practical mapping model rather than full BGP best-path selection.

### Snapshot

Internal state can be persisted and restored.

Snapshot design goals:

* preserve enough information for correct withdraw handling after restart

* remain human-readable

* separate peer registry from observations

Derived export tables are rebuilt from internal state after loading.

### Export

The system exports:

* prefix_to_asn.tsv

* asn_to_prefix.tsv

These are text-based artifacts intended for downstream tools and manual inspection.

### Growth Statistics and Plateau Detection

ASNPrefixMap tracks:

* unique prefixes ever seen

* unique ASNs ever seen

* active prefix counts

* per-interval discovery rates

Plateau detection is heuristic and reports when prefix growth stabilizes, indicating that the dataset is broadly complete.

## Design Principles

* Keep transport separate from parsing

* Keep parsing separate from routing-state logic

* Keep runtime storage compact

* Keep snapshot/export text-based and debuggable

* Prefer practical usefulness over full router-grade complexity