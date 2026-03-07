#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
cmake --build build -j"$(sysctl -n hw.logicalcpu)"

rm -rf '/Applications/GRAPHISOFT/Archicad 29/Add-Ons/OpenBrep.bundle'
cp -r build/OpenBrep.bundle '/Applications/GRAPHISOFT/Archicad 29/Add-Ons/'

(
  cd build
  zip -r OpenBrep-v0.1.0-AC29.zip OpenBrep.bundle
)

echo '✅ 部署完成'
echo '📦 打包完成：build/OpenBrep-v0.1.0-AC29.zip'
