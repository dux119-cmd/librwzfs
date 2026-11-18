# Read benchmark: host tree vs mounted archive

`examples/bench_read.cpp` (built as `rwzfs_bench` with dwarfs) compares reads of the same files on
a plain host tree and as a mounted archive. Both runs use the same wrapper calls and the same file
names; only the root changes (the tree directory, or `/rwz`).

## Data

Four content classes (how compressible the data is) by six sizes:

| class | content |
|---|---|
| zero | all zero bytes |
| runs | long runs of one byte value (64 KiB runs) |
| patterns | a short repeating 16-byte pattern, new for every 4 KiB |
| random | uniform random bytes |

Sizes: 100 B, 16 KiB, 256 KiB, 1 MiB, 100 MiB, 1 GiB. Full set: 24 files, about 4.4 GB.

## Running it

    rwzfs_bench compare /dev/shm/rwzbench --csv results.csv

`compare` does everything under the work directory: generates the tree (`tree/`), creates the
archive (`archive/bench.dwarfs`, written to `archive/`), benchmarks the host tree, mounts the
archive at `/rwz` with its overlay under `archive/writes`, benchmarks the mounted archive,
unmounts, and prints the tables and the comparison.

Other subcommands run one part at a time: `generate <dir>`, `archive <dir> <file>`,
`run <root>` (`<root>` is a host directory or `/rwz`).

Options: `--budget SEC` (time budget per file and operation, default 2), `--max-iters N` (default
500), `--sizes LIST` (for example `100,16K,1M,100M,1G`), `--csv FILE`, `--compression SPEC`.

**Resources.** The full set needs about 4.4 GB of tree in `/dev/shm`, plus the archive, plus the
overlay. `/dev/shm` is RAM, so plan for at least 10 GB of RAM (or a larger `/dev/shm`). Creating the
archive from 4.4 GB of data takes a while on one CPU. The default time budget gives roughly ten
minutes of measurement per root for the full set.

## What is measured

For each file and each operation, the benchmark repeats the operation until it reaches `--max-iters`
or the time budget (after at least 5 iterations). Large files get fewer iterations. The `iters`
column says how many were taken.

| operation | timed part |
|---|---|
| open | `open` only (close is untimed) |
| read head (4 KiB) | seek to 0, read 4 KiB |
| read tail (4 KiB) | seek to size − 4 KiB, read 4 KiB |
| read whole file | read the whole file in 128 KiB chunks |
| seek+read near start | seek to min(1 KiB, size/2), read 64 KiB |
| seek+read near end | seek to size − 64 KiB − 1 KiB, read 64 KiB |

Each file is opened for each iteration, outside the timed part except for `open` itself, so the
timed part is the same kind of work in every row.

## Statistics

Per (file, operation): iterations, min, p50, p90, p95, p99, p99.9, mean and standard deviation of
latency in microseconds. For reads, throughput in MB/s: the median of per-iteration throughput
(`MB/s p50`), and the aggregate (total bytes ÷ total time, `MB/s agg`). Percentiles use the
nearest-rank method on the sorted samples. The CSV has all of these, plus p95 and p99.9, for each
root.

The comparison table shows p50 latency and aggregate throughput for host and mounted, with the
ratio mounted ÷ host for latency and host ÷ mounted for throughput. A ratio above 1 for latency
means the mounted archive is slower; for throughput, above 1 means the mounted archive is slower.

## Reduced run (verification, not the full benchmark)

This machine has 3.9 GB of RAM, so the full set was not run. The run below uses sizes 100 B to
10 MiB, a 0.3 s budget and at most 40 iterations. Numbers are from a shared 1-CPU machine and are
only indicative. The full run should be done on a quiet machine.

| file | operation | host p50 µs | mounted p50 µs | ratio |
|---|---|---|---|---|
| any | open | ~24 | ~66 | ~2.7 |
| any 4 KiB read (head/tail) | read | ~0.6 | ~1.4 | ~2.4 |
| 16 KiB | read whole file | 0.8 | 1.4 | 1.9 |
| 256 KiB | read whole file | 10.0 | 8.4 | 0.8 |
| 10 MiB | read whole file | 586 | 528 | 0.9 |
| 10 MiB | seek+read near start / end | ~3 | ~3 | ~1.0 |

Reading the numbers:

- Opening a file through the overlay costs about 40 µs more than a host open. Each open does an
  archive lookup and an overlay check.
- Small reads pay the same fixed cost, so their ratio is about 2.5×. For larger files the fixed cost
  is spread over more bytes, and mounted throughput reaches host throughput.
- On this run the mounted archive was as fast as the host tree for 256 KiB and larger files.

The CSV from this run has 241 lines: one header and 240 result rows (2 roots × 20 files × 6 operations, less
the operations the tiny file cannot run).

## Limits of the benchmark

- Files are re-opened each iteration, so kernel and cache effects are included in every row.
  `/dev/shm` is RAM, so there is no disk I/O: this measures the library and the dwarfs reader, not
  storage.
- The archive is read through dwarfs's cache. The first iteration of a row may be slower than the
  rest; the p50 is not affected by a single slow iteration, but p99.9 can be.
- Only read operations are measured. Writes through the overlay are not included.
- The default compression is `zstd:level=12` (the benchmark sets it), not the default with `wlog` and
  `strat`.
