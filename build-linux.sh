#!/usr/bin/env bash
set -euo pipefail

zoo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
demo=""
while getopts ":d:h" option; do
  case "$option" in
    d) demo=$OPTARG ;;
    h) echo "Usage: $0 -d <zipformer|mms_tts|yolo26n_depth>"; exit 0 ;;
    *) echo "Usage: $0 -d <zipformer|mms_tts|yolo26n_depth>" >&2; exit 2 ;;
  esac
done

: "${RECAMERA_SYSROOT:?Set RECAMERA_SYSROOT to the reCamera Pro sysroot}"
: "${RECAMERA_RKNNRT:=$RECAMERA_SYSROOT/usr/lib/librknnrt.so}"
export RECAMERA_SYSROOT
export RECAMERA_RKNNRT

case "$demo" in
  zipformer) target=recamera_stt ;;
  mms_tts) target=recamera_tts_benchmark ;;
  yolo26n_depth) target=recamera_depth_rtsp ;;
  *) echo "Unsupported demo: $demo" >&2; exit 2 ;;
esac

source_dir="$zoo_root/examples/$demo/cpp"
build_dir="$zoo_root/build/$demo"
install_dir="$zoo_root/install/rv1126b_linux_aarch64/rknn_${demo}_demo"

if [[ "$demo" == "yolo26n_depth" ]]; then
  export PKG_CONFIG_SYSROOT_DIR="$RECAMERA_SYSROOT"
  export PKG_CONFIG_LIBDIR="$RECAMERA_SYSROOT/usr/lib/aarch64-linux-gnu/pkgconfig:$RECAMERA_SYSROOT/usr/lib/pkgconfig:$RECAMERA_SYSROOT/usr/share/pkgconfig"
  unset PKG_CONFIG_PATH
fi

cmake -S "$source_dir" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$zoo_root/cmake/recamera-aarch64.cmake" \
  -DRECAMERA_RKNNRT="$RECAMERA_RKNNRT"
cmake --build "$build_dir" --parallel
cmake -E make_directory "$install_dir"
cmake -E copy "$build_dir/$target" "$install_dir/$target"

file "$install_dir/$target"
readelf -l "$install_dir/$target" | grep 'Requesting program interpreter'
readelf -d "$install_dir/$target" | grep -E 'NEEDED|RPATH|RUNPATH'
