#!/bin/bash
echo "🧠 CPU Core–Thread Mapping"
echo "=========================="
echo "Core_ID | Logical Threads | Core Type"
echo "--------|-----------------|-----------"

declare -A seen

for f in /sys/devices/system/cpu/cpu*/topology/thread_siblings_list; do
    siblings=$(cat $f | tr '-' ',' | tr ',' ' ')
    first=$(echo $siblings | awk '{print $1}')
    if [[ -z "${seen[$siblings]}" ]]; then
        seen[$siblings]=1
        count=$(echo $siblings | wc -w)
        if [[ $count -eq 1 ]]; then
            type="E-core (1 thread)"
        else
            type="P-core (2 threads)"
        fi
        echo "$(printf '%-7s' "$first") | $(printf '%-15s' "$siblings") | $type"
    fi
done | sort -n -k1
