#!/bin/bash

# コンテナの同時起動数を定義
CONCURRENT_LIMIT=5

# サービス名の配列を取得
services=($(docker compose config --services))

# 失敗したサービス名を記録する
failed=()

# 各サービスをビルドする
for service in "${services[@]}"; do
    if ! docker compose build "$service"; then
        echo "Build failed: $service" >&2
        failed+=("$service")
    fi
done

# 起動ジョブの制御用関連変数の初期化
declare -A pids

# サービスを起動する関数
start_service() {
    local service="$1"
    echo "Starting service: $service"
    # --exit-code-from でコンテナの終了コードを docker compose の終了コードに反映する
    docker compose up --exit-code-from "$service" "$service" &
    pids[$!]=$service
}

# ジョブが1つ終了するまで待ち、終了コードを記録する関数
wait_one_job() {
    local pid service
    while true; do
        for pid in "${!pids[@]}"; do
            if ! kill -0 "$pid" 2>/dev/null; then
                service=${pids[$pid]}
                unset "pids[$pid]"
                if wait "$pid"; then
                    echo "Service $service finished"
                else
                    echo "Service $service failed" >&2
                    failed+=("$service")
                fi
                return 0
            fi
        done
        # 全てのジョブが実行中の場合、短いスリープ後に再確認
        sleep 1
    done
}

# サービスを起動する
for service in "${services[@]}"; do
    # イメージのビルドに失敗したサービスは起動しない
    if [[ " ${failed[*]} " == *" $service "* ]]; then
        continue
    fi
    # 実行中のジョブ数が CONCURRENT_LIMIT に達している間は終了を待つ
    while [ ${#pids[@]} -ge $CONCURRENT_LIMIT ]; do
        wait_one_job
    done
    start_service "$service"
done

# 全てのジョブが終了するまで待機
while [ ${#pids[@]} -gt 0 ]; do
    wait_one_job
done

if [ ${#failed[@]} -gt 0 ]; then
    echo "The following services failed: ${failed[*]}" >&2
    exit 1
fi

echo "All services have been completed successfully."
