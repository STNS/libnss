#!/bin/bash

if [[ $1 == "users?name=test" ]]; then
  echo "ok"
  exit 0
fi
exit 1
