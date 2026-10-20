#!/bin/bash
line=$(printf 'a%.0s' $(seq 1 1023))
for i in $(seq 1 20); do
  echo "$line"
done
