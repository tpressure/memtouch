# About

Simple but configurable memory stresstest for UNIX-like systems to benchmark live migration.

# How to Build

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

# How to use

```bash
Usage: memtouch [--help] [--version] --thread_mem VAR --num_threads VAR --rw_ratio VAR [--random] [--stat_file VAR] [--stat_ival VAR]

The measurements are given in Mebibyte per second. This means that increasing or 
decreasing the interval has no effect on the read/write measurements, but only
increase the accuracy in terms of time resolution.

Optional arguments:
  -h, --help     shows help message and exits
  -v, --version  prints version information and exits
  --thread_mem   amount of memory a thread touches in MiB [required]
  --num_threads  number of worker threads [required]
  --rw_ratio     read/write ratio where 0 means only reads and 100 only writes [required]
  --stat_file    filepath where statistics are logged
  --stat_ival    interval for statistics logging in ms
```

## Example

```bash
$ ./memtouch --thread_mem 256 --num_threads 4 --rw_ratio 50 --stat_file ./stats.log --stat_ival 100
Running 4 threads touching 256 MB of memory
    memory consumption : 1024 MB
    access pattern     : sequential
    r/w ratio          : 50
    statistics file    : ./stats.log
    statistics interval: 100 ms

# The measurements are given in Mebibyte per second.
$ cat stats.log
2024-10-29T21:51:03.315+0100 read_mibps:16809.49 write_mibps:17834.99
2024-10-29T21:51:03.415+0100 read_mibps:15886.61 write_mibps:17992.63
2024-10-29T21:51:03.515+0100 read_mibps:16129.03 write_mibps:17834.99
```
