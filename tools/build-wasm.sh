#!/usr/bin/env bash
# 플레이그라운드용 WASM 빌드 (docs/venos.js + docs/venos.wasm).
#
#   tools/build-wasm.sh          # 빌드하고 docs/ 를 갱신
#   tools/build-wasm.sh --check  # 빌드만 하고 docs/ 는 건드리지 않음 (CI 용)
#
# emcc 가 아니라 em++ 로 불러야 한다 — 요즘 emsdk 는 emcc 로 C++ 를 링크하면
# operator delete 미정의로 죽는다.
#
# ASYNCIFY 는 빼면 안 된다: input 이 이것 없이는 페이지를 통째로 얼린다.
set -eu
cd "$(dirname "$0")/.."

command -v em++ >/dev/null || {
    echo "em++ 가 없습니다. emsdk 를 설치하고 emsdk_env.sh 를 source 하세요." >&2
    exit 2
}

OUT=docs/venos.js
if [ "${1:-}" = "--check" ]; then
    OUT=$(mktemp -d)/venos.js
fi

em++ -O2 -std=c++17 -fexceptions -DVENOS_WASM venos.cpp -o "$OUT" \
  -s EXPORTED_FUNCTIONS=_venos_run,_venos_trace,_venos_topython,_venos_flush,_malloc,_free \
  -s EXPORTED_RUNTIME_METHODS=ccall \
  -s DISABLE_EXCEPTION_CATCHING=0 -s ALLOW_MEMORY_GROWTH=1 \
  -s TOTAL_STACK=33554432 -s INITIAL_MEMORY=67108864 \
  -s MODULARIZE=1 -s EXPORT_NAME=createVenos -s ENVIRONMENT=web \
  -s ASYNCIFY -s ASYNCIFY_STACK_SIZE=1048576

if [ "${1:-}" = "--check" ]; then
    echo "빌드 확인 완료 (docs/ 는 그대로)"
    exit 0
fi

# 커밋된 WASM 이 어느 소스에서 나왔는지 남긴다.
# CI 가 이 값으로 "소스는 바뀌었는데 WASM 은 안 바뀐" 상태를 잡는다.
sha256sum venos.cpp | cut -d' ' -f1 > docs/venos.wasm.source-sha256
echo "docs/venos.js, docs/venos.wasm 갱신 완료"
echo "소스 해시: $(cat docs/venos.wasm.source-sha256)"
echo
echo "다음: node tools/playground-check.js --future"
