# About

Simple but configurable memory stresstest for UNIX-like systems.

# How to Use and Build

## Nix Toolchain

```bash
git checkout <this repo>
nix build .
nix run . -- <args> # e.g.: nix run -- --help
```

## Regular Toolchain

```bash
git checkout <this repo>
git submodule update --init
make
./memtouch --help
```
