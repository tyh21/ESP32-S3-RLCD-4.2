# ssh2_exec

An ESP-IDF example project demonstrating the use of the
[ESP-IDF port of libssh2](https://github.com/skuodi/libssh2_esp) to run a
command on a remote SSH server. The example server configuration used here is for
[bandit.labs.overthewire.org](https://overthewire.org/wargames/bandit/bandit0.html).

## Prerequisites

1. [ESP-IDF](https://idf.espressif.com) installed system-wide such that
`idf.py` is available at the terminal.

2. [OpenSSH](https://www.openssh.com) installed system-wide such that
ssh-keygen is available at the terminal. OpenSSH is available by default on
most Linux- and Windows-based systems.

3. ESP32 with at least 4MB flash. A smaller flash size may be accomodated by
modifying [partitions.csv](partitions.csv) and the corresponding `menuconfig`
entry under `Serial flasher config` > `Flash size`.

## Usage

1. Clone or download the repo and `cd` to this example directory
```sh
git clone --recursive https://github.com/skuodi/libssh2_esp
cd libssh2_esp/examples/ssh2_exec
```

2. Run `idf.py menuconfig` and under `libssh2 Example Configuration`, modify
SSH-specific configurations such as the SSH server host IP/port, SSH login
username/password and the command to execute on the SSH server.
No DNS resolution is performed on the server host address so it must be
provided as a valid IPv4 address.<br>
To log in using public/private keypair instead of a password, enable
`Use public key authentication for SSH login` .

4. Under `Example Connection Configuration` set up your Wi-Fi/Ethernet
network configuration as necessary.

5. Build the project, flash to your ESP32 device and monitor the serial log.
```sh
idf.py build flash monitor
```

If public key authentication is enabled, the [data](data)
folder is checked during compilation for a valid private key with the
name supplied in `libssh2 Example Configuration` > `SSH private key file`.
If the file is found, `ssh-keygen` prompts the user on whether to overwrite the
file or generate a public key from the provided private key.
If no such file is found, a keypair is generated in the [data](data) folder
with `<configured filename>` as private keyfile and `<configured filename>.pub`
as the public key file. No passphrase is expected for the keypair.<br>
The generated key files are uploaded to the LittleFS root directory.

### Important
The default export format of `ssh-keygen` for recent versions of OpenSSH
generates a private key in a format that is currently not supported by mbedTLS
and parsing will fail at runtime.
Use the `-m PEM` flag when generating a private key using `ssh-keygen`
so that the output is always in a compatible format for mbedTLS:

```sh
ssh-keygen -t rsa -b 4096 -m PEM -f ${PRIVKEY_FILENAME} -C "" -N ""
```

### Example output

The following is output from running the example on the M5Stack
[M5StickCPlus2](https://docs.m5stack.com/en/core/M5StickC%20PLUS2).

![ssh2_exec](run.gif)

## License

Released under [BSD-3-Clause](/LICENSE) by [@skuodi](https://github.com/skuodi).