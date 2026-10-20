"""Summarize netbench raw.csv (mean, sample variance) and plot the experiments.

Usage: uv run --with matplotlib python bench/plot.py raw.csv outdir/
"""

import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

KEY = ("experiment", "format", "clients", "moving", "static", "iterations")
FORMATS = ("binary", "json")
Row = dict[str, str]


def summarize(raw_rows: list[Row]) -> list[Row]:
    groups: dict[tuple[str, ...], list[Row]] = defaultdict(list)
    for row in raw_rows:
        groups[tuple(row[k] for k in KEY)].append(row)

    summary = []
    for key, rows in groups.items():
        seconds = [float(r["seconds"]) for r in rows]
        variance = statistics.variance(seconds) if len(seconds) > 1 else 0.0
        summary.append(
            dict(zip(KEY, key))
            | {
                "runs": str(len(rows)),
                "mean_s": f"{statistics.mean(seconds):.6f}",
                "variance_s2": f"{variance:.9f}",
                "stddev_s": f"{variance**0.5:.6f}",
                "mean_ms_per_iter": f"{statistics.mean(float(r['ms_per_iter']) for r in rows):.6f}",
                "snapshot_bytes": f"{statistics.mean(int(r['snapshot_bytes']) for r in rows):.0f}",
            }
        )
    return summary


def series(
    summary: list[Row], experiment: str, fmt: str, x: str, y: str
) -> tuple[list[int], list[float], list[float]]:
    rows = sorted(
        (r for r in summary if r["experiment"] == experiment and r["format"] == fmt),
        key=lambda r: int(r[x]),
    )
    return (
        [int(r[x]) for r in rows],
        [float(r[y]) for r in rows],
        [float(r["stddev_s"]) for r in rows],
    )


def line_plot(
    summary: list[Row], experiment: str, y: str, labels: tuple[str, str], path: Path
) -> None:
    fig, ax = plt.subplots()
    for fmt in FORMATS:
        xs, ys, errs = series(summary, experiment, fmt, experiment, y)
        ax.errorbar(xs, ys, yerr=errs if y == "mean_s" else None, marker="o", capsize=4, label=fmt)
    ax.set_xlabel(labels[0])
    ax.set_ylabel(labels[1])
    ax.set_title(f"{experiment}: {summary[0]['iterations']} iterations per client")
    ax.legend()
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)


def static_plot(summary: list[Row], path: Path) -> None:
    fig, ax = plt.subplots()
    width = 0.35
    for i, fmt in enumerate(FORMATS):
        statics, means, errs = series(summary, "static", fmt, "static", "mean_s")
        positions = [p + (i - 0.5) * width for p in range(len(statics))]
        ax.bar(positions, means, width, yerr=errs, capsize=4, label=fmt)
        ax.set_xticks(range(len(statics)), [f"{s} static" for s in statics])
    ax.set_ylabel("Mean run time (s)")
    ax.set_title(f"static: 100 moving, 2 clients, {summary[0]['iterations']} iterations")
    ax.legend()
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)


def main(raw_path: Path, out_dir: Path) -> None:
    with raw_path.open(newline="") as f:
        summary = summarize(list(csv.DictReader(f)))
    out_dir.mkdir(parents=True, exist_ok=True)
    with (out_dir / "summary.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)

    line_plot(
        summary,
        "moving",
        "mean_s",
        ("Moving objects", "Mean run time (s)"),
        out_dir / "fig1_moving.png",
    )
    line_plot(
        summary,
        "clients",
        "mean_s",
        ("Clients", "Mean run time (s)"),
        out_dir / "fig2_clients.png",
    )
    static_plot(summary, out_dir / "fig3_static.png")
    line_plot(
        summary,
        "moving",
        "snapshot_bytes",
        ("Moving objects", "Snapshot size (bytes)"),
        out_dir / "fig4_bytes.png",
    )


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(Path(sys.argv[1]), Path(sys.argv[2]))
