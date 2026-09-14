#!/bin/bash
set -x

TARGET_PATH="/home/defend/ec_prototype/storage"
SERVERS_FILE="/home/defend/ec_prototype/servers.txt"

if [ ! -f "$SERVERS_FILE" ]; then
    echo "error: can ont find $SERVERS_FILE!"
    exit 1
fi

while IFS= read -r server || [ -n "$server" ]; do
    server=$(echo "$server" | tr -d '\r' | xargs)
    [ -z "$server" ] && continue

    echo ">>> 正在处理: $server"
    ssh -n "$server" "rm -rf ${TARGET_PATH} && mkdir -p ${TARGET_PATH}"
    
    if [ $? -eq 0 ]; then
        echo "    [Success] $server cleanup completed"
    else
        echo "    [Failed]  $server cleanup failed, please check"
    fi
done < "$SERVERS_FILE"
