# reCamera Pro Model Zoo

[简体中文](README_CN.md) | English

Native RKNN examples dedicated to **reCamera Pro (RV1126B, aarch64)**. Unlike the upstream multi-platform RKNN Model Zoo, this repository has one fixed target and reuses the board-compatible RKNN Runtime at `/userdata/rknn_test/3rdparty/lib/librknnrt.so`.

## Available examples

| Category | Example | Model | Status |
| --- | --- | --- | --- |
| Speech recognition | [zipformer](examples/zipformer/README.md) | Streaming bilingual Chinese-English Zipformer | Ready |
| Text to speech | [mms_tts](examples/mms_tts/README.md) | English MMS-TTS | Ready |

The existing `yolov5_benchmark` and `yolov8n_pose` directories are work in progress and are not part of this restructuring yet.

## Layout

```text
.
├── 3rdparty/rknn/include/       # RKNN Toolkit2 2.3.2 C API header
├── cmake/                       # RV1126B/aarch64 toolchain
├── docs/                        # repository-level benchmark reports
├── examples/<model>/
│   ├── cpp/                     # native RKNN Runtime application
│   ├── model/                   # ONNX/RKNN models and metadata
│   ├── README.md                # English
│   └── README_CN.md             # 简体中文
├── install/rv1126b_linux_aarch64/ # reusable board executables
└── build-linux.sh               # common cross-build entry point
```

## Build

```bash
export RECAMERA_SYSROOT=/absolute/path/to/recamera-pro-sysroot
export RECAMERA_RKNNRT="$RECAMERA_SYSROOT/usr/lib/librknnrt.so"
./build-linux.sh -d zipformer
./build-linux.sh -d mms_tts
```

Model conversion, when needed, must use RKNN-Toolkit2 2.3.2 with `target_platform='rv1126b'`. The build embeds `/userdata/rknn_test/3rdparty/lib` as RUNPATH and never replaces the runtime already installed on the camera.

See [benchmark results](docs/benchmark.md).

## Upstream

The organization follows [airockchip/rknn_model_zoo](https://github.com/airockchip/rknn_model_zoo), narrowed to RV1126B Linux only.

