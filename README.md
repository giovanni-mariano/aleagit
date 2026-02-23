# aleagit

A geometry-aware Git CLI for nuclear models. It understands MCNP and OpenMC geometry files at the structural level — cells, surfaces, materials, universes — so you can diff, blame, and validate CSG models the same way you diff source code. Built on [libgit2](https://libgit2.org/) and [libalea](https://github.com/giovanni-mariano/libalea.c).

Runs alongside standard `git` in any existing repository. No server, no hooks required.

**The tool is under active development. Commands and output format may change.**

## Quick Start

```bash
# In any git repo with MCNP or OpenMC geometry files
aleagit status              # Which cells/surfaces changed?
aleagit diff                # Structural diff against HEAD
aleagit log --cell 42       # When did cell 42 last change?
aleagit blame               # Who last modified each element?
aleagit validate            # Parse check + overlap detection
```

## What It Does

- **Semantic diff** — Compare geometry by structure, not by text. Whitespace, comments, and formatting are ignored. Changes are reported per cell and per surface with detailed flags (material, density, region, fill, lattice)
- **Per-element history** — Filter `log` and `blame` by individual cell or surface ID
- **Overlap detection** — Find overlapping cells before they waste compute time
- **Format agnostic** — MCNP (`.inp`, `.i`, `.mcnp`) and OpenMC (`.xml`) files are loaded into the same internal representation; all commands work identically on both
- **Geometry-aware commits** — `aleagit commit` appends a structured trailer listing every cell and surface that changed

## Install

### Pre-built binaries

Download the latest release for your platform from the [Releases](https://github.com/giovanni-mariano/aleagit/releases) page. Archives are available for Linux (x64, arm64), macOS (x64, arm64), and Windows (x64).

```bash
# Example: Linux x64
tar xzf aleagit-linux-x64.tar.gz
sudo cp aleagit-linux-x64/aleagit /usr/local/bin/
```

The binary is dynamically linked against libgit2, which must be installed on your system:

| Platform | Install libgit2 |
|----------|----------------|
| Debian/Ubuntu | `sudo apt install libgit2-1.9` (or whichever version is available) |
| Fedora | `sudo dnf install libgit2` |
| macOS | `brew install libgit2` |
| Windows (MSYS2) | `pacman -S mingw-w64-ucrt-x86_64-libgit2` |

### Build from source

```bash
git clone --recursive https://github.com/giovanni-mariano/aleagit.git
cd aleagit
make
```

If you already cloned without `--recursive`, fetch the submodule separately:

```bash
git submodule update --init
make
```

Then copy or symlink the `aleagit` binary somewhere on your `PATH`.

#### Build options

```bash
make                          # Debug build
make RELEASE=1                # Optimized build (-O3, native arch)
make RELEASE=1 PORTABLE=1    # Optimized build without -march=native
make clean                    # Remove all build artifacts
```

#### Build dependencies

| Dependency | How it's used | How to get it |
|------------|---------------|---------------|
| C11 compiler (gcc or clang) | Build everything | System package manager |
| libgit2 development headers | Repository access, blob reading, history walking | `apt install libgit2-dev` or equivalent |
| [libalea](https://github.com/giovanni-mariano/libalea.c) | Geometry parsing, CSG queries, slice rendering, overlap detection | Included as git submodule, built automatically |

To update the libalea submodule to the latest upstream:

```bash
make submodule-update
```

## Commands

| Command | Description |
|---------|-------------|
| `aleagit init [--hook]` | Configure `.gitattributes` for geometry files; optionally install pre-commit validation hook |
| `aleagit summary [rev]` | Print cell, surface, and universe counts at a revision |
| `aleagit status` | Show which geometry files changed, with per-element change counts |
| `aleagit diff [rev1] [rev2]` | Structural diff between revisions (defaults to HEAD vs working tree) |
| `aleagit log [--cell N] [--surface N]` | Per-element change history |
| `aleagit blame [--cell N] [--surface N]` | Who last modified each cell and surface |
| `aleagit validate [--pre-commit]` | Parse check and overlap detection |
| `aleagit add <files>` | Stage files for commit |
| `aleagit commit -m "msg"` | Commit with geometry change trailer |

## How It Works

Each cell and surface is fingerprinted for fast comparison. Scalar fields (material, density, universe, fill, boundary type) are compared directly. Variable-size structures — the CSG region tree and lattice fill arrays — are reduced to 64-bit FNV-1a hashes. Two fingerprint sets are compared with a two-pointer merge on sorted element IDs, producing per-element added/removed/modified status with detailed change flags.

Visual diffs render both geometry versions on a 2D grid via libalea's slice API. When no axis is specified, 20 candidate positions are sampled per axis on a coarse 32x32 grid and the slice with the most differing pixels is selected. Surface contours are rasterized analytically from libalea's curve output.

## Project Structure

```
include/
  aleagit.h            Shared enums, constants, change flag bitfields
src/
  main.c               Command dispatcher
  cmd_init.c            init command
  cmd_summary.c         summary command
  cmd_status.c          status command
  cmd_diff.c            diff command (text)
  cmd_diff_visual.c     diff command (visual)
  cmd_log.c             log command
  cmd_blame.c           blame command
  cmd_validate.c        validate command
  cmd_add.c             add command
  cmd_commit.c          commit command
  git_helpers.{c,h}     libgit2 wrappers
  geom_load.{c,h}       Format detection and geometry loading
  geom_fingerprint.{c,h}  FNV-1a hashing of cells and surfaces
  geom_diff.{c,h}       Two-pointer merge diff
  visual_diff.{c,h}     Grid rendering, contour stamping, smart slice selection
  bmp_writer.{c,h}      24-bit BMP output
  util.{c,h}            Color TTY output, error/warning helpers
vendor/
  libalea/              libalea git submodule (built from source)
hooks/
  pre-commit            Pre-commit hook for geometry validation
```

## Disclaimer

This package was developed with support of AI tools.

## License

Mozilla Public License 2.0 (MPL-2.0).

Built on [libalea](https://github.com/giovanni-mariano/libalea.c) and [libgit2](https://libgit2.org/).
