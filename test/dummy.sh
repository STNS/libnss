#!/bin/bash
if [[ $1 == "users?name=test" || $1 == "test" ]]; then
  echo -e "aaabbbccc\nddd"
  exit 0
fi
