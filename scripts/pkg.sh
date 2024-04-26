#!/bin/bash
pids=()
SUPPORTOS="centos7 almalinux9 ubuntu20 ubuntu22 ubuntu24 debian10 debian11"
rm -rf builds && mkdir builds
for i in $SUPPORTOS; do
  {
    docker-compose build nss_$i && docker-compose run --rm -v "$(pwd)":/stns nss_$i
  } 2>&1 | tee builds/$i.log &

  pids+=($!)
done

for pid in ${pids[@]}; do
  wait $pid
  if [ $? -ne 0 ]; then
    exit 1
  fi
done
rm -f  builds/*log
