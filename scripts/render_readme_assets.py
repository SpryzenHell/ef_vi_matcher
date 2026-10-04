#!/usr/bin/env python3
"""Generate README figures from a captured EFVI CI reference run.

The script uses only the Python standard library. It converts the checked-in
text/CSV evidence into deterministic SVG figures so the README images can be
regenerated without a graphics package.
"""

from __future__ import annotations

import csv
import html
import re
import sys
from pathlib import Path


BG = "#17191d"
PANEL = "#20242a"
BORDER = "#4e5662"
TEXT = "#edf0f4"
MUTED = "#a8afb9"
ACCENT = "#79a9ff"
GOOD = "#87c38f"
WARN = "#e5b96a"


def esc(value: object) -> str:
    return html.escape(str(value), quote=True)


def shell_svg(title: str, lines: list[str], source: str, width: int = 1200) -> str:
    line_h = 34
    top = 115
    height = max(300, top + len(lines) * line_h + 85)

    chunks = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        f'<rect width="100%" height="100%" fill="{BG}"/>',
        f'<rect x="24" y="24" width="{width-48}" height="{height-48}" rx="12" fill="{PANEL}" stroke="{BORDER}" stroke-width="2"/>',
        '<circle cx="52" cy="50" r="6" fill="#d36d6d"/>',
        '<circle cx="74" cy="50" r="6" fill="#d9bd67"/>',
        '<circle cx="96" cy="50" r="6" fill="#83bc8c"/>',
        f'<text x="125" y="57" fill="{TEXT}" font-family="DejaVu Sans, sans-serif" font-size="22">{esc(title)}</text>',
    ]

    y = top
    for line in lines:
        fill = ACCENT if line.startswith("$ ") else TEXT
        chunks.append(
            f'<text x="52" y="{y}" fill="{fill}" font-family="DejaVu Sans Mono, monospace" '
            f'font-size="19">{esc(line)}</text>'
        )
        y += line_h

    chunks.append(
        f'<text x="52" y="{height-48}" fill="{MUTED}" font-family="DejaVu Sans Mono, monospace" '
        f'font-size="14">Source: {esc(source)}</text>'
    )
    chunks.append("</svg>")
    return "\n".join(chunks)


def card_svg(title: str, cards: list[tuple[str, str]], source: str) -> str:
    width, height = 1200, 520
    chunks = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        f'<text x="60" y="65" fill="#23282f" font-family="DejaVu Sans, sans-serif" font-size="31">{esc(title)}</text>',
    ]

    cols = 2
    card_w, card_h = 500, 130
    x_positions = [60, 640]
    y = 110

    for i, (label, value) in enumerate(cards):
        row = i // cols
        col = i % cols
        x = x_positions[col]
        yy = y + row * 165
        chunks.extend([
            f'<rect x="{x}" y="{yy}" width="{card_w}" height="{card_h}" rx="12" fill="#f6f7f9" stroke="#c7cdd5" stroke-width="2"/>',
            f'<text x="{x+24}" y="{yy+40}" fill="#6b737e" font-family="DejaVu Sans, sans-serif" font-size="18">{esc(label)}</text>',
            f'<text x="{x+24}" y="{yy+86}" fill="#20252b" font-family="DejaVu Sans Mono, monospace" font-size="28">{esc(value)}</text>',
        ])

    chunks.append(
        f'<text x="60" y="{height-30}" fill="#7b828c" font-family="DejaVu Sans Mono, monospace" '
        f'font-size="14">Source: {esc(source)}</text>'
    )
    chunks.append("</svg>")
    return "\n".join(chunks)


def bars_svg(title: str, labels: list[str], values: list[float], unit: str, source: str) -> str:
    width, height = 1200, 620
    left, top, chart_w, chart_h = 90, 120, 1020, 360
    max_v = max(values) if values else 1.0
    max_v *= 1.15

    chunks = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        f'<text x="60" y="65" fill="#23282f" font-family="DejaVu Sans, sans-serif" font-size="31">{esc(title)}</text>',
        f'<text x="60" y="94" fill="#707883" font-family="DejaVu Sans, sans-serif" font-size="17">Unit: {esc(unit)}</text>',
    ]

    for tick in range(6):
        frac = tick / 5
        y = top + chart_h - frac * chart_h
        tick_value = max_v * frac
        chunks.append(
            f'<line x1="{left}" y1="{y:.1f}" x2="{left+chart_w}" y2="{y:.1f}" stroke="#e0e4e9" stroke-width="1"/>'
        )
        chunks.append(
            f'<text x="{left-12}" y="{y+5:.1f}" text-anchor="end" fill="#7a828c" '
            f'font-family="DejaVu Sans Mono, monospace" font-size="13">{tick_value:.3f}</text>'
        )

    n = len(values)
    gap = 90
    bar_w = (chart_w - gap * (n + 1)) / max(n, 1)

    for i, (label, value) in enumerate(zip(labels, values)):
        x = left + gap + i * (bar_w + gap)
        bar_h = value / max_v * chart_h
        y = top + chart_h - bar_h
        chunks.extend([
            f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_w:.1f}" height="{bar_h:.1f}" rx="6" fill="{ACCENT}"/>',
            f'<text x="{x+bar_w/2:.1f}" y="{y-12:.1f}" text-anchor="middle" fill="#2e353d" '
            f'font-family="DejaVu Sans Mono, monospace" font-size="16">{value:.3f}</text>',
            f'<text x="{x+bar_w/2:.1f}" y="{top+chart_h+40}" text-anchor="middle" fill="#4e5660" '
            f'font-family="DejaVu Sans, sans-serif" font-size="16">{esc(label)}</text>',
        ])

    chunks.append(
        f'<text x="60" y="{height-28}" fill="#7b828c" font-family="DejaVu Sans Mono, monospace" '
        f'font-size="14">Source: {esc(source)}</text>'
    )
    chunks.append("</svg>")
    return "\n".join(chunks)


def architecture_svg() -> str:
    width, height = 1400, 760
    boxes = [
        (50, 155, 250, 125, "NIC / DPDK", "optional ingress"),
        (310, 155, 250, 125, "RX buffer", "rte_mbuf or software"),
        (570, 155, 250, 125, "Descriptor ring", "SPSC, 64-byte slots"),
        (830, 155, 250, 125, "OrderRequest", "read in place"),
        (1090, 155, 250, 125, "Matcher", "price-time priority"),
        (310, 420, 250, 125, "Order pool", "O(1) allocation"),
        (570, 420, 250, 125, "Price ladder", "FIFO at each level"),
        (830, 420, 250, 125, "Order index", "fixed open addressing"),
        (1090, 420, 250, 125, "Trade sink", "deterministic sequence"),
    ]

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<text x="50" y="68" fill="#23282f" font-family="DejaVu Sans, sans-serif" font-size="32">Runtime data path</text>',
        '<text x="50" y="100" fill="#707883" font-family="DejaVu Sans, sans-serif" font-size="17">Maintained software path; DPDK is optional.</text>',
    ]

    for x, y, w, h, title, sub in boxes:
        parts.extend([
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="14" fill="#f7f8fa" stroke="#bfc6cf" stroke-width="2"/>',
            f'<text x="{x+w/2}" y="{y+52}" text-anchor="middle" fill="#25303a" font-family="DejaVu Sans, sans-serif" font-size="21">{esc(title)}</text>',
            f'<text x="{x+w/2}" y="{y+83}" text-anchor="middle" fill="#69727d" font-family="DejaVu Sans, sans-serif" font-size="16">{esc(sub)}</text>',
        ])

    arrows = [
        (300, 217, 310, 217), (560, 217, 570, 217), (820, 217, 830, 217), (1080, 217, 1090, 217),
        (1215, 280, 1215, 420),
        (435, 345, 435, 420),
        (690, 345, 690, 420),
        (955, 345, 955, 420),
        (1080, 482, 1090, 482),
    ]
    for x1, y1, x2, y2 in arrows:
        parts.append(
            f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{ACCENT}" stroke-width="5" marker-end="url(#arrow)"/>'
        )

    parts.extend([
        '<defs><marker id="arrow" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M 0 0 L 10 5 L 0 10 z" fill="#79a9ff"/></marker></defs>',
        '<text x="50" y="690" fill="#717984" font-family="DejaVu Sans Mono, monospace" font-size="15">No background matcher thread is used in the reference implementation.</text>',
        "</svg>",
    ])
    return "\n".join(parts)


def layout_svg() -> str:
    width, height = 1400, 650
    rows = [
        ("OrderRequest", "64 B", "alignas(64)"),
        ("OrderNode", "64 B", "one cache line"),
        ("Level", "64 B", "one cache line"),
        ("SPSC Slot", "64 B", "aligned slot"),
        ("RxDescriptor", "16 B", "16-byte payload"),
    ]

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<text x="50" y="60" fill="#23282f" font-family="DejaVu Sans, sans-serif" font-size="32">Data layout used by the matcher</text>',
        '<text x="50" y="92" fill="#707883" font-family="DejaVu Sans, sans-serif" font-size="17">Sizes and alignment are checked with static assertions where appropriate.</text>',
    ]

    y = 135
    for name, size, note in rows:
        parts.extend([
            f'<text x="60" y="{y+26}" fill="#2c333b" font-family="DejaVu Sans, sans-serif" font-size="20">{esc(name)}</text>',
            f'<rect x="325" y="{y}" width="540" height="40" rx="5" fill="#edf2f8" stroke="#aab7c7" stroke-width="2"/>',
            f'<text x="595" y="{y+27}" text-anchor="middle" fill="#33404e" font-family="DejaVu Sans Mono, monospace" font-size="18">{esc(size)}</text>',
            f'<text x="920" y="{y+26}" fill="#6d7580" font-family="DejaVu Sans, sans-serif" font-size="18">{esc(note)}</text>',
        ])
        y += 92

    parts.extend([
        '<text x="50" y="620" fill="#7b828c" font-family="DejaVu Sans Mono, monospace" font-size="14">Source: include/efvi/types.hpp and include/efvi/order_book.hpp</text>',
        "</svg>",
    ])
    return "\n".join(parts)


def ci_summary_svg() -> str:
    width, height = 1200, 590
    rows = [
        ("Configure release", "PASS"),
        ("Build release", "PASS"),
        ("CTest release", "PASS"),
        ("Smoke examples", "PASS"),
        ("ASan/UBSan build", "PASS"),
        ("ASan/UBSan tests", "PASS"),
    ]
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<text x="60" y="64" fill="#23282f" font-family="DejaVu Sans, sans-serif" font-size="32">CI validation summary</text>',
        '<text x="60" y="96" fill="#707883" font-family="DejaVu Sans, sans-serif" font-size="17">GitHub Actions run 61 on the documentation branch.</text>',
    ]

    y = 140
    for label, result in rows:
        parts.extend([
            f'<rect x="60" y="{y}" width="1080" height="52" rx="8" fill="#f5f7f8" stroke="#d1d6dc" stroke-width="1.5"/>',
            f'<text x="84" y="{y+33}" fill="#2c333b" font-family="DejaVu Sans, sans-serif" font-size="19">{esc(label)}</text>',
            f'<text x="1085" y="{y+33}" text-anchor="end" fill="{GOOD}" font-family="DejaVu Sans Mono, monospace" font-size="19">{esc(result)}</text>',
        ])
        y += 66

    parts.extend([
        '<text x="60" y="555" fill="#7b828c" font-family="DejaVu Sans Mono, monospace" font-size="14">Source: GitHub Actions run 37224231989</text>',
        "</svg>",
    ])
    return "\n".join(parts)


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print("usage: render_readme_assets.py <reference_run_dir> [output_dir]", file=sys.stderr)
        return 2

    src = Path(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) == 3 else Path("docs/images")
    out.mkdir(parents=True, exist_ok=True)

    matcher = (src / "matcher_smoke.txt").read_text().splitlines()
    software_rx = (src / "software_rx_loopback.txt").read_text().splitlines()
    order_flow = (src / "order_flow_demo.txt").read_text().splitlines()
    hugepage = (src / "hugepage_probe.txt").read_text().splitlines()
    strict = (src / "hugepage_strict.txt").read_text().splitlines()
    benchmark_console = (src / "benchmark_console.txt").read_text().splitlines()
    allocator = (src / "allocator_benchmark.txt").read_text().splitlines()
    environment = [
        (src / "system.txt").read_text().strip(),
        (src / "compiler.txt").read_text().splitlines()[0],
        "DPDK: not installed in the reference runner",
        "1 GiB huge pages: 0",
        "2 MiB huge pages: 0",
    ]

    with (src / "benchmark.csv").open(newline="") as f:
        row = next(csv.DictReader(f))

    throughput = float(row["ops_per_sec"]) / 1_000_000.0
    p50 = float(row["p50_us"])
    p99 = float(row["p99_us"])
    spsc = None
    for line in benchmark_console:
        match = re.search(r"spsc_items_per_sec=([0-9.]+)", line)
        if match:
            spsc = float(match.group(1)) / 1_000_000.0
            break

    files = {
        "application_run.svg": shell_svg("efvi_matcher", matcher, "matcher_smoke.txt"),
        "software_rx_demo.svg": shell_svg("software RX loopback", software_rx, "software_rx_loopback.txt"),
        "order_flow_demo.svg": shell_svg("efvi_order_flow_demo", order_flow, "order_flow_demo.txt"),
        "benchmark_run.svg": shell_svg("efvi_benchmark 50000", benchmark_console, "benchmark_console.txt"),
        "allocator_run.svg": shell_svg("efvi_allocator_benchmark 10000", allocator, "allocator_benchmark.txt"),
        "hugepage_probe.svg": shell_svg("efvi_hugepage_probe", hugepage, "hugepage_probe.txt"),
        "hugepage_strict_failure.svg": shell_svg("strict 1 GiB huge-page request", strict, "hugepage_strict.txt"),
        "environment.svg": shell_svg("reference environment", environment, "system.txt + compiler.txt"),
        "benchmark_throughput.svg": bars_svg(
            "Reference throughput — CI run",
            ["Matcher", "SPSC"],
            [throughput, spsc if spsc is not None else 0.0],
            "million operations/items per second",
            "benchmark.csv + benchmark_console.txt",
        ),
        "benchmark_latency.svg": bars_svg(
            "Reference matcher latency — CI run",
            ["p50", "p99"],
            [p50, p99],
            "microseconds",
            "benchmark.csv",
        ),
        "architecture.svg": architecture_svg(),
        "data_layout.svg": layout_svg(),
        "ci_tests.svg": ci_summary_svg(),
    }

    for filename, content in files.items():
        (out / filename).write_text(content, encoding="utf-8")

    print(f"generated {len(files)} figures in {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
