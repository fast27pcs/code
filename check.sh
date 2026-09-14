#!/bin/bash
set -e
# nodes for testbed
ARRAY=('192.168.0.234' '192.168.0.235' '192.168.0.236' '192.168.0.237' '192.168.0.238' '192.168.0.239' '192.168.0.240' '192.168.0.241' '192.168.0.242' '192.168.0.243' '192.168.0.244' '192.168.0.245' '192.168.0.246' '192.168.0.247' '192.168.0.248' '192.168.0.249')
NUM=${#ARRAY[@]}
echo "cluster_number:"$NUM
NUM=`expr $NUM - 1`

for i in $(seq 0 $NUM)
do
temp=${ARRAY[$i]}
    echo $temp
    ssh roe@$temp 'ps -aux | grep run_datanode | wc -l'
    ssh roe@$temp 'ps -aux | grep run_proxy | wc -l'
done