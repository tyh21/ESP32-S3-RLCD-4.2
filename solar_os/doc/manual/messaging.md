+++
id = "messaging"
title = "Messaging, contacts, and credential security"
section = "service"
summary = "Provider-neutral messaging identities, trust, persistence, and secret handling"
aliases = ["contacts.service", "credentials"]
keywords = "messaging contacts credentials trust endpoint gateway meshcore security"
packages_any = ["service_contacts", "service_credentials"]
+++
# Messaging identities and security

SolarOS separates messaging identity from transport. A contact is one local
address-book entry, while each gateway, MeshCore, or SolarOS Link address is an
endpoint with its own trust and capabilities. Linking endpoints does not copy
trust between providers.

Trust has three states:

- `discovered`: a provider supplied a valid address or advert;
- `trusted`: the user explicitly accepts that endpoint identity;
- `blocked`: direct inbound and outbound messaging is denied.

A signed MeshCore advert proves ownership of its public key, not the human
identity using the device. New adverts therefore remain `discovered` until the
user trusts them.

Contacts keeps at most 64 contacts and 80 endpoints in PSRAM. Its bounded,
CRC-checked store uses alternating headers and data copies under
`/.contacts/contacts.bin`. Persistence writes and `fsync` occur after releasing
the service lock. If the filesystem is unavailable, the service stays usable
in volatile mode and reports the error through `contacts status`.

Credentials keeps at most 12 opaque NVS records with at most 128 secret bytes
each. Supported record kinds are asymmetric identities, shared keys, and
tokens. Public listing exposes only the record ID, provider, kind, and label.
Secret reads name one record and copy into a caller-owned buffer; temporary
buffers are wiped.

Credentials are never exposed through Inbox, logs, autocomplete, native-agent
tools, Python, or Lua. The current SolarOS NVS configuration is not encrypted,
so someone with physical flash access can recover the stored secrets. Flash
and NVS encryption provisioning are outside this release.

SSH keys, native-agent API keys, and existing provider tokens remain in their
current stores and are not migrated automatically.

See `contacts` for the TUI and mutation commands. `chat` is the unified
conversation UI and works without Wi-Fi; provider jobs connect transports when
available. The `messages` shell API exposes the same bounded conversation
store. Stable 64-bit message IDs are printed as hexadecimal strings so they
survive shell and scripting runtimes without precision loss.

Direct messages to a blocked endpoint are always rejected. A discovered
endpoint requires an explicit second confirmation in Chat or
`--allow-untrusted` in the shell. That opt-in applies only to the one send and
does not change the endpoint's trust state.

## Quick reference

```text
contacts
contacts status
contacts list [all|discovered|trusted|blocked]
contacts show CONTACT_ID
contacts rename CONTACT_ID NAME
contacts trust CONTACT_ID [ENDPOINT_ID]
contacts block CONTACT_ID [ENDPOINT_ID]
contacts remove CONTACT_ID
contacts link TARGET_CONTACT_ID SOURCE_CONTACT_ID
messages status
messages conversations
messages list CONVERSATION_ID
messages send CONVERSATION_ID TEXT [--allow-untrusted]
messages read CONVERSATION_ID
messages delete MESSAGE_ID
messages clear gateway|meshcore|link|all
messages outbox
messages cancel MESSAGE_ID
outbox [list]
outbox cancel MESSAGE_ID
```

The three user-facing collections have distinct roles: Conversations contain
retained message history, Inbox is a universal notification view of inbound
items from messaging and other applications, and Outbox contains only pending
sends. Provider packet queues and event rings are internal bounded transport
mechanisms, not additional mailboxes. On internal flash, Messaging physically
shares Inbox's compact persistent records but still owns the conversation API.

`messages clear` also removes the selected provider's Inbox projections,
including stale projections whose retained ring slot has already wrapped. It
does not remove unrelated Inbox sources; use `inbox clear` for the entire
universal Inbox.

For packet-radio operation, see `meshcore` and `link`. MeshCore direct messages
use the same conversations and trust states as gateway messages; shared-key
groups remain visibly sender-unverified. A `radio-link` or `espnow-link` job
started with `chat=on` adds unencrypted, packet-sized Link broadcast and direct
conversations and discovers source device IDs as Contacts.
