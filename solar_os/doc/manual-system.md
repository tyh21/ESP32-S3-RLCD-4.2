# Unified device manual

SolarOS keeps GitHub user documentation, device help, website pages, and agent
API guidance in one canonical source: `doc/manual/*.md`. Each topic is an
ordinary Markdown guide with TOML frontmatter and a final `Quick reference`
section. `doc/manual/README.md` is a generated GitHub index over the same topic
metadata.

The build generator reads those pages and creates a package-gated C registry.
`man` and text-shell `help` display a terminal-normalized form, graphic display
shells open the Markdown in `reader`, and the agent's `solaros_reference` tool
returns the compact `Quick reference` section. The website generator renders
the same files as HTML. Topic IDs, groups, aliases, summaries, keywords, and
package gates therefore cannot drift between those interfaces.

## Topic format

```markdown
+++
id = "python.gfx"
title = "Python graphics"
section = "api"
summary = "Draw safely on the active display"
aliases = ["gfx"]
keywords = "python graphics display"
packages_any = ["app_python"]
+++
# Python graphics

Tutorial text...

## Quick reference

The concise, authoritative contract used by the agent...
```

The `id` is the runtime lookup key, and the filename must be `<id>.md`. IDs and
aliases must be unique. `section` is one of the ordered documentation-tree
groups defined by the generator. `packages_any` contains package IDs from
`packages/solar_os_packages.toml`. A topic is embedded when any named package is
present; an empty list makes it universal.

## Release and refresh

The deployment pipeline copies the Markdown tree to
`/ota/<version>/doc/manual/`, creates `doc/manual.zip`, generates
`doc/catalog.json`, and signs the exact catalog bytes as `doc/catalog.sig` with
the OTA release key. The schema-v2 catalog authenticates the archive path, byte
count, and SHA-256. Each page entry also contains its extracted path, byte
count, SHA-256, metadata, and Quick reference.

On a Wi-Fi, PSRAM, and SD capable build, `help update` resolves the configured
OTA URL to the exact running firmware version. The device verifies the catalog
signature, rejects version mismatches, downloads the one authenticated archive,
extracts it into a temporary revision, and checks every page's signed size and
SHA-256 before changing the small active-revision pointer. Existing readers
retain valid paths because activated revisions are not deleted at runtime.

Downloaded pages override only the body and Quick reference of topics already
compiled into the firmware. Search metadata and package availability remain
the embedded registry's responsibility. If the SD card, active pointer,
signature, catalog, archive, page, or parser is unavailable, each lookup falls back to
the embedded copy.

Use:

```text
help status
help update
help reset
```

`help reset` removes the active pointer and immediately restores the embedded
manual. Cached immutable revision directories may remain on SD.
