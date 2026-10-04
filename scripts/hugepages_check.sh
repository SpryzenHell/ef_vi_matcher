#!/usr/bin/env bash
set -euo pipefail

echo "Kernel hugepage summary:"
grep -E 'HugePages|Hugepagesize|Hugetlb' /proc/meminfo || true
echo
echo "1 GiB pool:"
cat /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages 2>/dev/null || echo "unavailable"
echo "2 MiB pool:"
cat /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages 2>/dev/null || echo "unavailable"
echo
echo "Allocator probe:"
./build/efvi_hugepage_probe
