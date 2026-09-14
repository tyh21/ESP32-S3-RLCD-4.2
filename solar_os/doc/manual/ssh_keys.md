+++
id = "ssh_keys"
title = "SSH identity keys"
section = "network"
summary = "Inspect, share, generate, and remove the default SSH key pair"
aliases = ["sshkeys", "sshkey"]
keywords = "python lua ssh scp keys identity private public generate remove credentials"
packages_any = ["service_ssh"]
+++
# SSH identity keys

SolarOS can keep a default SSH key pair for `ssh` and `scp`. The public key may
be copied to remote hosts; the private key must remain protected.

## Inspect or create a key

```text
sshkey status
sshkey gen 2048
sshkey pub
```

Generating a key may take time and memory. Without `-f`, an existing key is not
overwritten.

## Protect private material

Do not print, copy, edit, or delete the private key unless the user explicitly
requests that exact operation. The agent's storage tools reject paths below
`.ssh`.

## Quick reference

solaros.ssh_keys provides default_paths, default_exists, status, public_key,
generate (optional bits and overwrite), and remove when SSH is installed.
`public_key()` returns only the default OpenSSH public-key line. Do not inspect,
expose, replace, or delete private key material unless the user explicitly
requests that operation.
