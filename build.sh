#!/bin/bash

# コンテナの同時起動数を定義
CONCURRENT_LIMIT=4

# サービス名の配列を取得
services=($(docker compose config --services))

# 各サービスをビルドする（省略可能）
for service in "${services[@]}"; do
    docker compose build "$service"
done

# 起動ジョブの制御用関連変数の初期化
declare -A pids
declare -A services_started

# サービスを起動する関数
start_service() {
    local service="$1"
    echo "Starting service: $service"
    docker compose up "$service" &
    pids[$!]=$service
    services_started[$service]=1
}

# ジョブが終了したか監視し、終わっていれば別のサービスを起動する関数
watch_jobs() {
    while [ ${#pids[@]} -ge $CONCURRENT_LIMIT ]; do
        for pid in "${!pids[@]}"; do
            if ! kill -0 "$pid" 2>/dev/null; then
                # ジョブが存在しない場合
                local finished_service=${pids[$pid]}
                echo "Service $finished_service finished"
                unset pids[$pid]
                return 0
            fi
        done
        # 全てのジョブが実行中の場合、短いスリープ後に再確認
        sleep 1
    done

    return 1
}

# サービスを起動する
for service in "${services[@]}"; do
    # 既に起動されているかチェック
    if [[ ! ${services_started[$service]} ]]; then
        # 現在実行しているジョブの数がCONCURRENT_LIMIT未満時、またはジョブの終了を検出したときに新しいサービスを起動
        if [[ ${#pids[@]} -lt $CONCURRENT_LIMIT ]] || watch_jobs; then
            start_service "$service"
        fi
    fi
done

# 全てのジョブが終了するまで待機
for pid in "${!pids[@]}"; do
    wait "$pid"
    echo "Service ${pids[$pid]} finished"
done

echo "All services have been started."
