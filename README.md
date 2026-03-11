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
- offline testing using JSONL event streams

## Example

Input events:

{"type":"announce","peer":"rrc00|192.0.2.1|64500","prefix":"8.8.8.0/24","asn":15169}

Output:
8.8.8.0/24 15169

## Build

mkdir build
cd build
cmake ..
make

## Roadmap

- WebSocket BGP source (RIS Live)
- MRT file replay
- IPv6 support
- ASN metadata enrichment
- IP longest-prefix lookup
