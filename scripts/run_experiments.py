#!/usr/bin/env python3
"""Run a repeatable benchmark matrix and save the raw analysis data."""

from __future__ import annotations

import argparse
import csv
import re
import statistics
import subprocess
import tempfile
from pathlib import Path


BENCH_SIZES = [10_000, 25_000, 50_000, 100_000, 200_000]
ALLOC_SIZES = [1_000, 5_000, 10_000, 50_000, 100_000]
REPEATS = 3
SPSC_REPEATS = 2


def run(cmd: list[str], cwd: Path | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, text=True, capture_output=True, check=check)


def parse_kv(line: str) -> dict[str, str]:
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


def parse_benchmark(stdout: str) -> dict[str, str]:
    lines = stdout.splitlines()
    matcher = next(line for line in lines if line.startswith("operations="))
    spsc = next(line for line in lines if line.startswith("spsc_items="))
    row = parse_kv(matcher)
    row.update({f"spsc_{k}": v for k, v in parse_kv(spsc).items()})
    return row


def write_csv(path: Path, rows: list[dict[str, object]], fields: list[str]) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--output-dir", default="docs/reference_run/current")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    build = (root / args.build_dir).resolve()
    out = (root / args.output_dir).resolve()
    out.mkdir(parents=True, exist_ok=True)

    bench_exe = build / "efvi_benchmark"
    alloc_exe = build / "efvi_allocator_benchmark"
    spsc_exe = build / "efvi_spsc_benchmark"
    probe_exe = build / "efvi_hugepage_probe"

    bench_rows: list[dict[str, object]] = []
    alloc_rows: list[dict[str, object]] = []
    spsc_rows: list[dict[str, object]] = []
    page_rows: list[dict[str, object]] = []
    console_lines: list[str] = []

    with tempfile.TemporaryDirectory(prefix="efvi-exp-") as tmp:
        tmp_path = Path(tmp)

        for n in BENCH_SIZES:
            for rep in range(1, REPEATS + 1):
                result = run([str(bench_exe), str(n), "--normal"], cwd=tmp_path)
                parsed = parse_benchmark(result.stdout)
                bench_rows.append({
                    "operations": n,
                    "repeat": rep,
                    "ops_per_sec": parsed["ops_per_sec"],
                    "p50_us": parsed["p50_us"],
                    "p99_us": parsed["p99_us"],
                    "trades": parsed["trades"],
                    "live_orders": parsed["live_orders"],
                    "pool_used": parsed["pool_used"],
                    "pool_mapped_bytes": parsed["mapped_bytes"],
                    "page_size": parsed["page_size"],
                    "hugepages": parsed["hugepages"],
                    "hotpath_allocations": parsed["hotpath_allocations"],
                })
                console_lines.append(f"benchmark n={n} repeat={rep} normal")
                console_lines.extend(f"  {line}" for line in result.stdout.splitlines())

        for objects in ALLOC_SIZES:
            for rep in range(1, REPEATS + 1):
                result = run([str(alloc_exe), str(objects)], cwd=tmp_path)
                line = next(line for line in result.stdout.splitlines() if line.startswith("objects="))
                parsed = parse_kv(line)
                alloc_rows.append({
                    "objects": objects,
                    "repeat": rep,
                    "alloc_ops_per_sec": parsed["alloc_ops_per_sec"],
                    "free_ops_per_sec": parsed["free_ops_per_sec"],
                    "stride": parsed["stride"],
                    "mapped_bytes": parsed["mapped_bytes"],
                })
                console_lines.append(f"allocator objects={objects} repeat={rep}")
                console_lines.append(f"  {line}")

        for rep in range(1, SPSC_REPEATS + 1):
            result = run([str(spsc_exe), "1000000"])
            lines = [line for line in result.stdout.splitlines() if line and line[0].isdigit()]
            for line in lines:
                capacity, items, rate, ring_align, slot_align = line.split(",")
                spsc_rows.append({
                    "repeat": rep,
                    "capacity": capacity,
                    "items": items,
                    "items_per_sec": rate,
                    "ring_alignment": ring_align,
                    "slot_alignment": slot_align,
                })
            console_lines.append(f"SPSC repeat={rep}")
            console_lines.extend(f"  {line}" for line in result.stdout.splitlines())

        probe = run([str(probe_exe)])
        (out / "hugepage_probe.txt").write_text(probe.stdout, encoding="utf-8")

        for mode in ["--normal", "--auto", "--huge2m", "--huge1g"]:
            result = run([str(bench_exe), "10000", mode], cwd=tmp_path)
            parsed = parse_benchmark(result.stdout)
            page_rows.append({
                "mode": mode.removeprefix("--"),
                "exit_code": result.returncode,
                "status": "huge-page mapping" if parsed["hugepages"] == "true" else "normal-page fallback",
                "detail": f"page_size={parsed['page_size']}, mapped_bytes={parsed['mapped_bytes']}, hugepages={parsed['hugepages']}",
            })
            console_lines.append(f"page-mode {mode}")
            console_lines.extend(f"  {line}" for line in result.stdout.splitlines())

        strict = run([str(bench_exe), "10000", "--huge1g", "--strict"], cwd=tmp_path, check=False)
        (out / "hugepage_strict.txt").write_text(
            strict.stdout + strict.stderr + f"strict_mode_exit_code={strict.returncode}\n",
            encoding="utf-8",
        )
        if strict.returncode not in (0, 2):
            raise RuntimeError(f"strict huge-page run returned unexpected code {strict.returncode}")
        if strict.returncode == 0:
            page_rows.append({
                "mode": "huge1g-strict",
                "exit_code": 0,
                "status": "huge-page mapping",
                "detail": "strict request succeeded on this host",
            })
        else:
            page_rows.append({
                "mode": "huge1g-strict",
                "exit_code": strict.returncode,
                "status": "expected refusal",
                "detail": "strict request failed because 1 GiB HUGETLB pages were unavailable",
            })

    write_csv(
        out / "benchmark_matrix.csv",
        bench_rows,
        ["operations", "repeat", "ops_per_sec", "p50_us", "p99_us", "trades",
         "live_orders", "pool_used", "pool_mapped_bytes", "page_size",
         "hugepages", "hotpath_allocations"],
    )
    write_csv(
        out / "allocator_matrix.csv",
        alloc_rows,
        ["objects", "repeat", "alloc_ops_per_sec", "free_ops_per_sec", "stride", "mapped_bytes"],
    )
    write_csv(
        out / "spsc_matrix.csv",
        spsc_rows,
        ["repeat", "capacity", "items", "items_per_sec", "ring_alignment", "slot_alignment"],
    )
    write_csv(
        out / "page_modes.csv",
        page_rows,
        ["mode", "exit_code", "status", "detail"],
    )

    benchmark_rates = [float(r["ops_per_sec"]) for r in bench_rows]
    allocator_rates = [float(r["alloc_ops_per_sec"]) for r in alloc_rows]
    free_rates = [float(r["free_ops_per_sec"]) for r in alloc_rows]
    spsc_rates = [float(r["items_per_sec"]) for r in spsc_rows]

    analysis = [
        "EFVI experiment summary",
        "",
        f"Matcher runs: {len(bench_rows)} ({len(BENCH_SIZES)} sizes × {REPEATS} repeats)",
        f"Matcher throughput median: {statistics.median(benchmark_rates):.3f} ops/s",
        f"Matcher throughput range: {min(benchmark_rates):.3f} .. {max(benchmark_rates):.3f} ops/s",
        f"Allocator runs: {len(alloc_rows)} ({len(ALLOC_SIZES)} sizes × {REPEATS} repeats)",
        f"Allocator throughput median: {statistics.median(allocator_rates):.3f} alloc/s",
        f"Free throughput median: {statistics.median(free_rates):.3f} free/s",
        f"SPSC runs: {len(spsc_rows)} ({3} capacities × {SPSC_REPEATS} repeats)",
        f"SPSC throughput median: {statistics.median(spsc_rates):.3f} items/s",
        "",
        "These are descriptive measurements from this recorded host.",
        "They are not cross-machine guarantees and are not used to infer a TLB reduction percentage.",
        "",
    ]
    (out / "analysis.txt").write_text("\n".join(analysis), encoding="utf-8")
    (out / "experiment_console.txt").write_text("\n".join(console_lines) + "\n", encoding="utf-8")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
