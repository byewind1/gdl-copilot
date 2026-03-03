#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
cmake --build build -j"$(sysctl -n hw.logicalcpu)"

rm -rf '/Applications/GRAPHISOFT/Archicad 29/Add-Ons/OpenBrep.bundle'
cp -r build/OpenBrep.bundle '/Applications/GRAPHISOFT/Archicad 29/Add-Ons/'

echo '✅ 部署完成'
