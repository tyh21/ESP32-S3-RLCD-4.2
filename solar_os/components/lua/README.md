# Lua Component

This component vendors the Lua 5.4.8 source from the official Lua download area:

https://www.lua.org/ftp/lua-5.4.8.tar.gz

SHA-256:

```text
4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
```

SolarOS builds the Lua VM and pure standard libraries only. The upstream command-line
frontends (`lua.c`, `luac.c`), dynamic package loader, `io`, and `os` libraries are
not linked into the embedded runtime.
