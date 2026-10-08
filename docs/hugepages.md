# Huge pages

The fixed pool supports:
- normal Linux pages;
- explicit 2 MiB HUGETLB pages;
- explicit 1 GiB HUGETLB pages;
- automatic selection with normal-page fallback.

Check the host:

    ./scripts/hugepages_check.sh

Strict 1 GiB mode:

    ./build/efvi_benchmark 10000 --huge1g --strict

Strict mode returns an error when the requested HUGETLB mapping is unavailable. Automatic mode does not report a huge-page run unless the mapping actually succeeds.

Use the recorded experiment data in docs/reference_run/current/page_modes.csv for the checked-in CI host.

A TLB reduction percentage must be measured on the target CPU with matched normal-page and strict-1-GiB runs and processor-specific perf counters.
