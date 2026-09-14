# MeshCore protocol subset

This directory vendors the minimum protocol subset audited for SolarOS from
`meshcore-dev/MeshCore` commit
`03b6ef4b0de98fc70b49ef10a6d0d61f8381fb7a`.

Included upstream surfaces are Dispatcher, Mesh, Packet, Identity, Utils,
BaseChatMesh, advert/text helpers, contact/channel records, packet pools and
duplicate tables, plus upstream's bundled Ed25519 implementation. AES-128 and
SHA-256 come from the MIT-licensed `rweather/Crypto` 0.4.0 implementation used
by that revision. `LICENSE.meshcore.txt`, `LICENSE.crypto.txt`, and
`ed25519/license.txt` retain the applicable notices.

SolarOS deliberately excludes Arduino UI and board code, BLE/Wi-Fi companion
protocols, RadioLib, repeaters, room servers, sensors, and telemetry. The local
`Arduino.h`, `Stream.h`, and `base64.hpp` files are compile-time compatibility
shims only; they do not provide or depend on the Arduino runtime.

Local audit patches:

- trailing source whitespace is normalized for the SolarOS tree;
- signature verification uses the included `ed25519_verify()` implementation
  directly instead of the optional Arduino Crypto Ed25519 wrapper;
- Arduino filesystem helpers in `SimpleMeshTables` are disabled;
- the Crypto AES header selects its ESP32 implementation from the
  `MESHCORE_SOLAROS` build definition as SolarOS does not define Arduino's
  global `ESP32` macro;
- allocation and radio ownership live in the SolarOS adapter, outside this
  upstream subtree;
- `Dispatcher.h` includes `"Packet.h"` rather than `<Packet.h>` so the
  sibling header always wins. The libssh2 component publishes its private
  `libssh2/src` directory on the global include path, and on case-insensitive
  filesystems (macOS APFS) `<Packet.h>` otherwise resolves to libssh2's
  `packet.h`.

Upstream updates are explicit dependency bumps: replace the audited files,
review the local patches, update the pinned hash above, and run the MeshCore
host/runtime suites. Nothing is downloaded during a firmware build.
