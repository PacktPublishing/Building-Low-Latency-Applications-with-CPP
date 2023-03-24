#!/bin/bash

CMAKE="/home/sghosh/.local/share/JetBrains/Toolbox/apps/CLion/ch-0/222.3739.54/bin/cmake/linux/bin/cmake"
NINJA="/home/sghosh/.local/share/JetBrains/Toolbox/apps/CLion/ch-0/222.3739.54/bin/ninja/linux/ninja"

mkdir -p cmake-build-release
$CMAKE -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=$NINJA -G Ninja -S . -B cmake-build-release

$CMAKE --build cmake-build-release --target clean -j 4
$CMAKE --build cmake-build-release --target all -j 4
