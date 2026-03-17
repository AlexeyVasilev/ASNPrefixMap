# ASNPrefixMap

ASNPrefixMap is a C++ tool for building a practical mapping
between IP prefixes and origin ASNs using BGP observations.

The system aggregates BGP announcements from multiple peers
and derives a stable prefix -> ASN mapping.

## Features

- prefix-centric routing state
- per-peer BGP observations
- correct withdraw handling
- export of prefix -> ASN tables
- export of ASN -> prefix reverse index
- configuration via config.ini
- offline replay of raw RIS Live JSON messages
- live RIPE RIS Live WebSocket ingestion
- snapshot save/load of internal routing observations
- runtime peer registry with numeric PeerId
- binary runtime prefix storage for IPv4 and IPv6
- optional periodic growth statistics CSV output
- small-vector optimization for per-prefix observations

## Ingestion Flow

The source layer always returns raw RIS Live JSON messages.
The main pipeline is:

source -> raw JSON -> parser -> BgpEvent -> PeerRegistry -> binary prefix parse -> RoutingState

`file_jsonl` is a replay source for raw RIS Live JSON lines.
`ris_live_ws` is a live WebSocket source using the same parser flow.

## Growth Stats

`stats_output_enabled` enables periodic CSV sampling.
`stats_output_file` selects the CSV path.
`stats_interval_ms` sets the sampling interval.
`stop_on_keypress` enables a local Enter-to-stop helper for interactive runs.

Ever-seen ASN/prefix counts are tracked separately from active counts so temporary withdrawals do not reset growth history.
Periodic sampling is intended for empirical growth measurement and future saturation estimation, not readiness detection yet.

## Snapshots

`snapshot_input` restores internal per-peer observations before new messages are processed.
`snapshot_output` saves the internal routing state on normal shutdown.

Snapshots preserve the internal observations required for correct withdraw handling after restart.
They do not store only the final prefix -> ASN export, because the export alone loses per-peer state.
Snapshot and export remain text-based so they stay easy to inspect manually, while runtime keeps normalized binary prefixes.

## Runtime State

Runtime state stores peer references as numeric `PeerId` values via `PeerRegistry`.
It now stores IPv4 and IPv6 prefixes in separate binary maps. Prefixes remain text only at parser, snapshot, and export boundaries.
Observation storage uses `boost::container::small_vector` with inline capacity 4 because most prefixes are expected to have only a few peer observations.

## Example

Input events:

{"type":"ris_message","data":{"type":"UPDATE","host":"rrc00","peer":"192.0.2.1","peer_asn":64500,"timestamp":1710000000,"path":[64500,64496,15169],"announcements":[{"prefixes":["8.8.8.0/24"]}]}}

Output:
8.8.8.0/24 15169

## Build

mkdir build
cd build
cmake ..
make

## Roadmap

- MRT file replay
- IPv6 support
- ASN metadata enrichment
- IP longest-prefix lookup
