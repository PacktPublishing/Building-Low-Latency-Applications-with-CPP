#!/bin/bash

for f in $(ls cmake-build*/*_example); do
  echo "Running "$f"...";
  ./$f
done
