#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

VERSION="$(/usr/bin/python3 - <<'PY'
from pathlib import Path
import re

content = Path('Sources/AddOnVersion.hpp').read_text(encoding='utf-8')
match = re.search(r'#define\s+ADDON_VERSION\s+"([^"]+)"', content)
if match is None:
    raise SystemExit('未在 Sources/AddOnVersion.hpp 中找到 ADDON_VERSION')
print(match.group(1))
PY
)"
TAG="v${VERSION}"
ZIP_NAME="OpenBrep-v${VERSION}-AC29.zip"

cmake --build build -j"$(sysctl -n hw.logicalcpu)"

rm -rf '/Applications/GRAPHISOFT/Archicad 29/Add-Ons/OpenBrep.bundle'
cp -r build/OpenBrep.bundle '/Applications/GRAPHISOFT/Archicad 29/Add-Ons/'

(
  cd build
  rm -f "${ZIP_NAME}"
  zip -r "${ZIP_NAME}" OpenBrep.bundle
)

echo '✅ 部署完成'
echo "📦 打包完成：build/${ZIP_NAME}"

if ! command -v gh >/dev/null 2>&1; then
  echo '❌ 未找到 gh，请先执行：brew install gh'
  exit 1
fi

if git rev-parse -q --verify "refs/tags/${TAG}" >/dev/null 2>&1 || git ls-remote --exit-code --tags origin "refs/tags/${TAG}" >/dev/null 2>&1; then
  echo "🏷️ 版本 tag 已存在，跳过 GitHub Release：${TAG}"
else
  gh release create "${TAG}" "build/${ZIP_NAME}" --title "OpenBrep v${VERSION}" --notes '自动发布' --latest
fi
