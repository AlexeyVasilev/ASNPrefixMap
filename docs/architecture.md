# Architecture

ASNPrefixMap maintains BGP observations per prefix and peer.

Instead of implementing a full BGP router, the system builds
a practical prefix -> origin ASN mapping.

## State model

prefix -> peer -> observation

Example:

8.8.8.0/24

peer A -> AS15169  
peer B -> AS15169

## Aggregation

The exported mapping selects the ASN that is observed from
the largest number of peers.

## Withdraw handling

Withdraw events remove only the observation from the
specific peer.

The prefix remains active if other peers still advertise it.

## Exported tables

prefix_to_asn.tsv


8.8.8.0/24 15169


asn_to_prefix.tsv


15169 8.8.8.0/24
