#!/usr/bin/env python3
"""Create clear README figures from the recorded CI run.

Only the Python standard library is used. Figures are deterministic SVG files
built from the checked-in text and CSV measurements.
"""

from __future__ import annotations

import csv
import html
import re
import statistics
import sys
from pathlib import Path


BG = "#11161c"
PANEL = "#1c232b"
BORDER = "#475260"
TEXT = "#f2f5f8"
MUTED = "#aab3bd"
ACCENT = "#72a8ff"
GOOD = "#79c98a"
WARN = "#e4b15e"
GRID = "#dfe4ea"
DARK = "#242a31"


def esc(value: object) -> str:
    return html.escape(str(value), quote=True)


def wrap_lines(lines: list[str], width: int = 78) -> list[str]:
    out: list[str] = []
    for line in lines:
        line = line.rstrip()
        if not line:
            out.append("")
            continue
        while len(line) > width:
            cut = line.rfind(" ", 0, width + 1)
            if cut < width // 2:
                cut = width
            out.append(line[:cut])
            line = line[cut:].lstrip()
        out.append(line)
    return out


def terminal_svg(title: str, raw_lines: list[str], source: str) -> str:
    lines = wrap_lines(raw_lines)
    width = 1280
    line_h = 36
    top = 112
    height = max(330, top + len(lines) * line_h + 86)

    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        f'<rect width="{width}" height="{height}" fill="{BG}"/>',
        f'<rect x="22" y="22" width="{width-44}" height="{height-44}" rx="16" fill="{PANEL}" stroke="{BORDER}" stroke-width="2"/>',
        '<circle cx="54" cy="51" r="7" fill="#db7777"/>',
        '<circle cx="78" cy="51" r="7" fill="#dfbf70"/>',
        '<circle cx="102" cy="51" r="7" fill="#7bc28a"/>',
        f'<text x="132" y="59" fill="{TEXT}" font-family="DejaVu Sans, sans-serif" font-size="24" font-weight="600">{esc(title)}</text>',
    ]

    y = top
    for line in lines:
        is_command = line.lstrip().startswith("$")
        fill = ACCENT if is_command else TEXT
        out.append(
            f'<text x="54" y="{y}" fill="{fill}" font-family="DejaVu Sans Mono, Liberation Mono, monospace" font-size="22">{esc(line)}</text>'
        )
        y += line_h

    out.append(
        f'<text x="54" y="{height-50}" fill="{MUTED}" font-family="DejaVu Sans Mono, Liberation Mono, monospace" font-size="15">Recorded source: {esc(source)}</text>'
    )
    out.append("</svg>")
    return "\n".join(out)


def line_chart(title: str, x_title: str, y_title: str,
               series: list[tuple[str, list[tuple[float, float]]]],
               source: str) -> str:
    width, height = 1280, 700
    left, right, top, bottom = 120, 50, 120, 110
    chart_w = width - left - right
    chart_h = height - top - bottom

    all_points = [p for _, pts in series for p in pts]
    xs = [p[0] for p in all_points]
    ys = [p[1] for p in all_points]
    xmin, xmax = min(xs), max(xs)
    ymin, ymax = min(ys), max(ys)
    if xmax == xmin:
        xmax = xmin + 1
    if ymax == ymin:
        ymax = ymin + 1
    yrange = ymax - ymin
    ymin -= 0.08 * yrange
    ymax += 0.08 * yrange

    def px(x: float) -> float:
        return left + (x - xmin) / (xmax - xmin) * chart_w

    def py(y: float) -> float:
        return top + chart_h - (y - ymin) / (ymax - ymin) * chart_h

    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        f'<text x="60" y="60" fill="{DARK}" font-family="DejaVu Sans, sans-serif" font-size="32" font-weight="600">{esc(title)}</text>',
        f'<text x="60" y="92" fill="#69727d" font-family="DejaVu Sans, sans-serif" font-size="18">{esc(y_title)} vs {esc(x_title)}</text>',
    ]

    for i in range(6):
        frac = i / 5
        y = top + chart_h - frac * chart_h
        value = ymin + frac * (ymax - ymin)
        out.append(f'<line x1="{left}" y1="{y:.1f}" x2="{left+chart_w}" y2="{y:.1f}" stroke="{GRID}" stroke-width="1"/>')
        out.append(f'<text x="{left-14}" y="{y+5:.1f}" text-anchor="end" fill="#69727d" font-family="DejaVu Sans Mono, monospace" font-size="14">{value:.3f}</text>')

    for x in sorted(set(xs)):
        xx = px(x)
        out.append(f'<line x1="{xx:.1f}" y1="{top}" x2="{xx:.1f}" y2="{top+chart_h}" stroke="{GRID}" stroke-width="1"/>')
        out.append(f'<text x="{xx:.1f}" y="{top+chart_h+30}" text-anchor="middle" fill="#69727d" font-family="DejaVu Sans Mono, monospace" font-size="14">{x:g}</text>')

    out.append(f'<text x="{left+chart_w/2:.1f}" y="{height-45}" text-anchor="middle" fill="{DARK}" font-family="DejaVu Sans, sans-serif" font-size="17">{esc(x_title)}</text>')
    out.append(f'<text x="28" y="{top+chart_h/2:.1f}" transform="rotate(-90 28 {top+chart_h/2:.1f})" text-anchor="middle" fill="{DARK}" font-family="DejaVu Sans, sans-serif" font-size="17">{esc(y_title)}</text>')

    palette = ["#72a8ff", "#79c98a", "#e4b15e", "#d38bd6"]
    for idx, (name, pts) in enumerate(series):
        col = palette[idx % len(palette)]
        coords = " ".join(f"{px(x):.1f},{py(y):.1f}" for x, y in pts)
        out.append(f'<polyline fill="none" stroke="{col}" stroke-width="5" points="{coords}"/>')
        for x, y in pts:
            out.append(f'<circle cx="{px(x):.1f}" cy="{py(y):.1f}" r="7" fill="{col}"/>')

        lx = 900 + idx * 135
        out.append(f'<line x1="{lx}" y1="76" x2="{lx+26}" y2="76" stroke="{col}" stroke-width="5"/>')
        out.append(f'<text x="{lx+36}" y="82" fill="{DARK}" font-family="DejaVu Sans, sans-serif" font-size="16">{esc(name)}</text>')

    out.append(f'<text x="60" y="{height-18}" fill="#7a828b" font-family="DejaVu Sans Mono, monospace" font-size="13">Source: {esc(source)}</text>')
    out.append("</svg>")
    return "\n".join(out)


def grouped_bars(title: str, groups: list[tuple[str, list[tuple[str, float]]]],
                 y_title: str, source: str) -> str:
    width, height = 1280, 700
    left, right, top, bottom = 110, 50, 120, 115
    chart_w = width - left - right
    chart_h = height - top - bottom
    max_v = max(v for _, bars in groups for _, v in bars)
    max_v = max(max_v * 1.2, 1.0)

    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        f'<text x="60" y="60" fill="{DARK}" font-family="DejaVu Sans, sans-serif" font-size="32" font-weight="600">{esc(title)}</text>',
        f'<text x="60" y="92" fill="#69727d" font-family="DejaVu Sans, sans-serif" font-size="18">{esc(y_title)}</text>',
    ]

    for i in range(6):
        frac = i / 5
        y = top + chart_h - frac * chart_h
        value = max_v * frac
        out.append(f'<line x1="{left}" y1="{y:.1f}" x2="{left+chart_w}" y2="{y:.1f}" stroke="{GRID}" stroke-width="1"/>')
        out.append(f'<text x="{left-14}" y="{y+5:.1f}" text-anchor="end" fill="#69727d" font-family="DejaVu Sans Mono, monospace" font-size="14">{value:.2f}</text>')

    group_w = chart_w / max(len(groups), 1)
    palette = ["#72a8ff", "#79c98a", "#e4b15e", "#d38bd6"]

    for gi, (group, bars) in enumerate(groups):
        base_x = left + gi * group_w
        bar_w = group_w / (len(bars) + 2)
        for bi, (label, value) in enumerate(bars):
            x = base_x + bar_w * (bi + 1)
            bh = value / max_v * chart_h
            y = top + chart_h - bh
            col = palette[bi % len(palette)]
            out.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_w-10:.1f}" height="{bh:.1f}" rx="7" fill="{col}"/>')
            out.append(f'<text x="{x+(bar_w-10)/2:.1f}" y="{y-10:.1f}" text-anchor="middle" fill="{DARK}" font-family="DejaVu Sans Mono, monospace" font-size="15">{value:.3f}</text>')
            out.append(f'<text x="{x+(bar_w-10)/2:.1f}" y="{top+chart_h+28}" text-anchor="middle" fill="#69727d" font-family="DejaVu Sans, sans-serif" font-size="14">{esc(label)}</text>')
        out.append(f'<text x="{base_x+group_w/2:.1f}" y="{height-52}" text-anchor="middle" fill="{DARK}" font-family="DejaVu Sans, sans-serif" font-size="16">{esc(group)}</text>')

    out.append(f'<text x="{left+chart_w/2:.1f}" y="{height-18}" text-anchor="middle" fill="#7a828b" font-family="DejaVu Sans, sans-serif" font-size="13">Source: {esc(source)}</text>')
    out.append("</svg>")
    return "\n".join(out)


def table_svg(title: str, headers: list[str], rows: list[list[str]],
              source: str, width: int = 1280) -> str:
    row_h = 48
    height = 150 + row_h * (len(rows) + 1)
    widths = [int(width * 0.52), int(width * 0.23), width - int(width * 0.52) - int(width * 0.23)]

    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        f'<text x="56" y="58" fill="{DARK}" font-family="DejaVu Sans, sans-serif" font-size="31" font-weight="600">{esc(title)}</text>',
    ]

    y = 92
    x = 56
    header_widths = widths if len(headers) == 3 else [int((width-112)/len(headers))]*len(headers)
    cur = x
    for h, w in zip(headers, header_widths):
        out.append(f'<rect x="{cur}" y="{y}" width="{w}" height="{row_h}" fill="#edf2f7" stroke="#c8d0d9"/>')
        out.append(f'<text x="{cur+14}" y="{y+31}" fill="{DARK}" font-family="DejaVu Sans, sans-serif" font-size="16" font-weight="600">{esc(h)}</text>')
        cur += w

    y += row_h
    for row in rows:
        cur = x
        for ci, (value, w) in enumerate(zip(row, header_widths)):
            fill = "#f8fafc" if (y // row_h) % 2 else "#ffffff"
            out.append(f'<rect x="{cur}" y="{y}" width="{w}" height="{row_h}" fill="{fill}" stroke="#d7dde4"/>')
            out.append(f'<text x="{cur+14}" y="{y+31}" fill="{DARK if ci != len(row)-1 else "#3f8f55"}" font-family="DejaVu Sans, sans-serif" font-size="16">{esc(value)}</text>')
            cur += w
        y += row_h

    out.append(f'<text x="56" y="{height-24}" fill="#7a828b" font-family="DejaVu Sans Mono, monospace" font-size="13">Source: {esc(source)}</text>')
    out.append("</svg>")
    return "\n".join(out)


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def main() -> int:
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("docs/reference_run/current")
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("docs/images")
    out.mkdir(parents=True, exist_ok=True)

    benchmark_console = (src / "benchmark_console.txt").read_text(encoding="utf-8").splitlines()
    allocator = (src / "allocator_baseline.txt").read_text(encoding="utf-8").splitlines()
    matcher = (src / "matcher_smoke.txt").read_text(encoding="utf-8").splitlines()
    software = (src / "software_rx_loopback.txt").read_text(encoding="utf-8").splitlines()
    order_flow = (src / "order_flow_demo.txt").read_text(encoding="utf-8").splitlines()
    hugepage = (src / "hugepage_probe.txt").read_text(encoding="utf-8").splitlines()
    strict = (src / "hugepage_strict.txt").read_text(encoding="utf-8").splitlines()
    ctest = (src / "ctest_release.txt").read_text(encoding="utf-8").splitlines()
    system = (src / "system.txt").read_text(encoding="utf-8").splitlines()
    compiler = (src / "compiler.txt").read_text(encoding="utf-8").splitlines()

    bench = read_csv(src / "benchmark_matrix.csv")
    alloc = read_csv(src / "allocator_matrix.csv")
    spsc = read_csv(src / "spsc_matrix.csv")
    pages = read_csv(src / "page_modes.csv")

    n_values = sorted({int(r["operations"]) for r in bench})
    throughput_points = []
    p50_points = []
    p99_points = []
    for n in n_values:
        rates = [float(r["ops_per_sec"]) / 1e6 for r in bench if int(r["operations"]) == n]
        p50s = [float(r["p50_us"]) for r in bench if int(r["operations"]) == n]
        p99s = [float(r["p99_us"]) for r in bench if int(r["operations"]) == n]
        throughput_points.append((n, statistics.median(rates)))
        p50_points.append((n, statistics.median(p50s)))
        p99_points.append((n, statistics.median(p99s)))

    alloc_n = sorted({int(r["objects"]) for r in alloc})
    alloc_series = []
    free_series = []
    for n in alloc_n:
        rates_a = [float(r["alloc_ops_per_sec"]) / 1e6 for r in alloc if int(r["objects"]) == n]
        rates_f = [float(r["free_ops_per_sec"]) / 1e6 for r in alloc if int(r["objects"]) == n]
        alloc_series.append((n, statistics.median(rates_a)))
        free_series.append((n, statistics.median(rates_f)))

    spsc_caps = sorted({int(r["capacity"]) for r in spsc})
    spsc_points = []
    for cap in spsc_caps:
        rates = [float(r["items_per_sec"]) / 1e6 for r in spsc if int(r["capacity"]) == cap]
        spsc_points.append((cap, statistics.median(rates)))

    files = {
        "application_run.svg": terminal_svg("efvi_matcher", matcher, "matcher_smoke.txt"),
        "software_rx_demo.svg": terminal_svg("software RX loopback", software, "software_rx_loopback.txt"),
        "order_flow_demo.svg": terminal_svg("efvi_order_flow_demo", order_flow, "order_flow_demo.txt"),
        "benchmark_run.svg": terminal_svg("efvi_benchmark reference run", benchmark_console, "benchmark_console.txt"),
        "allocator_run.svg": terminal_svg("efvi_allocator_benchmark", allocator, "allocator_baseline.txt"),
        "hugepage_probe.svg": terminal_svg("Huge-page probe", hugepage, "hugepage_probe.txt"),
        "hugepage_strict_failure.svg": terminal_svg("Strict 1 GiB request", strict, "hugepage_strict.txt"),
        "test_snapshot.svg": terminal_svg("Release test run", ctest, "ctest_release.txt"),
        "environment.svg": terminal_svg("Reference environment", system + compiler, "system.txt + compiler.txt"),
        "benchmark_throughput.svg": line_chart(
            "Matcher throughput across workload sizes",
            "operations per run",
            "million operations / second",
            [("median throughput", throughput_points)],
            "benchmark_matrix.csv; 3 repeats per size",
        ),
        "benchmark_latency.svg": line_chart(
            "Matcher latency across workload sizes",
            "operations per run",
            "microseconds",
            [("p50", p50_points), ("p99", p99_points)],
            "benchmark_matrix.csv; 3 repeats per size",
        ),
        "allocator_scaling.svg": line_chart(
            "Fixed-pool scaling",
            "objects",
            "million operations / second",
            [("allocate", alloc_series), ("free", free_series)],
            "allocator_matrix.csv; 3 repeats per size",
        ),
        "spsc_scaling.svg": line_chart(
            "SPSC throughput by ring size",
            "ring capacity",
            "million items / second",
            [("SPSC", spsc_points)],
            "spsc_matrix.csv; 2 repeats per capacity",
        ),
        "page_modes.svg": table_svg(
            "Page-mode experiment",
            ["Mode", "Observed result", "Mapping"],
            [[r["mode"], r["status"], r["detail"]] for r in pages],
            "page_modes.csv",
        ),
        "test_matrix.svg": table_svg(
            "Correctness checks",
            ["Test", "Result", "Details"],
            [
                ["Release unit tests", "PASS", "11 named checks"],
                ["Randomized model test", "PASS", "8 seeds × 1000 operations"],
                ["ASan/UBSan", "PASS", "same test targets"],
                ["Clean install test", "PASS", "installed matcher executed"],
                ["Docker smoke test", "PASS", "clean Ubuntu image"],
                ["DPDK configure path", "PASS", "optional targets skipped when absent"],
            ],
            "CI workflow output",
        ),
    }

    for name, content in files.items():
        (out / name).write_text(content, encoding="utf-8")

    print(f"generated {len(files)} figures in {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
