#!/usr/bin/env bash

for file in tests/*.mtc; do
  echo "$file"

  python compiler.py $file out
  read
done
