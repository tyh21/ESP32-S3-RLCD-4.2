# wireguard-lwip source pin

This component is based on `smartalock/wireguard-lwip` commit
`c54f20dbe76ac8b3411ad21e0ed7deea6f0cfd4d` from 2026-08-14.

SolarOS carries a local integration fork because the upstream component does
not provide a teardown operation, defaults to two allowed-IP entries, and
prints timing directly to standard output. The local changes also wipe decoded
keys, support initiator-only client operation, use the ESP-IDF 5.5 lwIP
dual-stack address accessors, and avoid a false fixed-array over-read warning
in the reference X25519 constant multiplication.

The upstream BSD 3-Clause license is retained in `LICENSE`. The X25519 source
has its additional notice in `src/crypto/refc/x25519-license.txt`.
