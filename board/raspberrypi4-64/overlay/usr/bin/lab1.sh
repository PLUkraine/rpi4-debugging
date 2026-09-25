#!/bin/sh

echo "Starting stress-ng"
/usr/bin/stress-ng -q --cpu 2 --cpu-load 80 --taskset 0,1 &
stress_ng_pid=$!

echo "Starting mmc_reader"
/usr/bin/mmc_reader /dev/mmcblk0 8192 &
mmc_reader_pid=$!

echo "type 'kill $stress_ng_pid; kill $mmc_reader_pid' to stop lab1 utilities"