#!/bin/bash

# コンテナの同時起動数を定義
CONCURRENT_LIMIT=4

# サービス名の配列を取得
services_list=($(docker-compose config --services))

# 各サービスをCONCURRENT_LIMITの数に応じて起動する
index=0
while [ $index -lt ${#services_list[@]} ]; do
  # 現在のバッチで起動するサービスのリストを生成
  services_to_start=()
  for ((i = 0; i < CONCURRENT_LIMIT && index+i < ${#services_list[@]}; i++)); do
    services_to_start+=(${services_list[index+i]})
  done

  echo "Starting services: ${services_to_start[*]}"

  # サービスを起動
  docker-compose build ${services_to_start[@]}
  docker-compose up ${services_to_start[@]}

  # 次のバッチのためのインデックス更新
  index=$((index + CONCURRENT_LIMIT))
done
