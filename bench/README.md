# netbench

Compares the Binary and JSON snapshot formats (`sceneReplicator::setFormat`) over the
client-server session.

## Run

```sh
cmake --preset macos-release && cmake --build --preset macos-release --target netbench
./build/macos-release/bench/netbench --out bench-results/raw.csv
uv run --with matplotlib python bench/plot.py bench-results/raw.csv bench-results/
```

Flags: `--iterations` (default 10000), `--runs` (default 5), `--out` (default `raw.csv`).
`plot.py` writes `summary.csv` (mean, sample variance, stddev per condition) and four figures.

## What is measured

Each run starts a fresh server with M moving and S static replicated boxes, and N clients
that each own one player. The server loop applies client states, moves the boxes and
publishes a snapshot, without sleeping. Each client does I iterations of: move the player,
`submitScene`, then spin until `applySnapshot` succeeds. One iteration is one game-loop round
trip, so `ms_per_iter` is also the per-client latency.

`seconds` is wall time from all clients starting together to the last one finishing.
`snapshot_bytes` is the size of the server's full snapshot after the run.

| Experiment | Varied | Fixed |
|---|---|---|
| moving | moving 10, 100, 500 | 2 clients, 0 static |
| clients | clients 1, 2, 4 | 100 moving, 0 static |
| static | static 0, 500 | 100 moving, 2 clients |

Formats alternate within each run so drift affects both equally. I is 10000 rather than
100000 because the full matrix is 80 runs and the 500-object JSON runs dominate.

## Limits

- Everything runs on localhost in one process: it measures serialization and the REQ/REP
  round trip, not real network latency.
- The server loop spins, and with 4 clients threads compete for cores. Record the core count
  (`sysctl -n hw.ncpu` on macOS, `nproc` on Linux) with the results.
