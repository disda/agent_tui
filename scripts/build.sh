#!/usr/bin/env bash
set -euo pipefail

build_dir="build"
config="Debug"
clean=0
run_tests=1
generator=""

usage() {
  cat <<'EOF'
Usage: scripts/build.sh [options]

Options:
  --build-dir DIR     Build directory (default: build)
  --config CONFIG     CMake build configuration (default: Debug)
  --release           Shortcut for --config Release
  --clean             Run the CMake clean target before building
  --no-tests          Skip ctest
  --generator NAME    Pass a CMake generator name
  -h, --help          Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      build_dir="${2:?--build-dir requires a value}"
      shift 2
      ;;
    --config)
      config="${2:?--config requires a value}"
      shift 2
      ;;
    --release)
      config="Release"
      shift
      ;;
    --clean)
      clean=1
      shift
      ;;
    --no-tests)
      run_tests=0
      shift
      ;;
    --generator)
      generator="${2:?--generator requires a value}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/.." && pwd)"
build_path="$repo_root/$build_dir"

run_step() {
  local name="$1"
  shift
  printf '\n==> %s\n' "$name"
  "$@"
}

cmake_args=(-S "$repo_root" -B "$build_path" "-DCMAKE_BUILD_TYPE=$config")
if [[ -n "$generator" ]]; then
  cmake_args+=(-G "$generator")
fi

run_step "Configure CMake project" cmake "${cmake_args[@]}"

if [[ "$clean" -eq 1 ]]; then
  run_step "Clean previous build outputs" cmake --build "$build_path" --config "$config" --target clean
fi

run_step "Build project" cmake --build "$build_path" --config "$config" --parallel

if [[ "$run_tests" -eq 1 ]]; then
  printf '\n==> Run tests\n'
  (cd "$build_path" && ctest -C "$config" --output-on-failure)
fi
