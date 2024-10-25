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
meson setup build
ninja -C build
./build/memtouch --help
```

## Code Formatting 

Format the files in place:

```bash
meson setup build # optional 
ninja -C build clang-format
```
