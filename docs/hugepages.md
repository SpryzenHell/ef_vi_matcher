# Huge pages

The fixed pool supports Linux HUGETLB mappings for both 2 MiB and 1 GiB pages.

Inspect the host with:

cat /proc/meminfo | grep -E 'HugePages|Hugepagesize|Hugetlb'
ls /sys/kernel/mm/hugepages/

Strict 1 GiB mode:

./build/efvi_benchmark 1000000 --huge1g --strict

Strict mode refuses normal-page fallback. If a 1 GiB hugetlb page is unavailable, the benchmark exits with an error.

The repository reports the actual backing page size and whether MAP_HUGETLB succeeded. A TLB-miss reduction percentage is not inferred from source code; collect processor-specific perf dTLB counters for matched normal-page and strict-1-GiB runs on the target CPU.
