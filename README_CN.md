# reCamera Pro Model Zoo

简体中文 | [English](README.md)

这是一个专用于 **reCamera Pro（RV1126B，aarch64）** 的原生 RKNN 示例仓库。与支持多平台的上游 RKNN Model Zoo 不同，本仓库只有一个固定目标，并复用板端 `/userdata/rknn_test/3rdparty/lib/librknnrt.so`。

## 已有示例

| 类别 | 示例 | 模型 | 状态 |
| --- | --- | --- | --- |
| 语音识别 | [zipformer](examples/zipformer/README_CN.md) | 流式中英双语 Zipformer | 可用 |
| 语音合成 | [mms_tts](examples/mms_tts/README_CN.md) | 英文 MMS-TTS | 可用 |
| 单目深度估计 | [yolo26n_depth](examples/yolo26n_depth/README_CN.md) | YOLO26n-Depth 与内嵌 RTSP 服务 | 可用 |

现有 `yolov5_benchmark` 和 `yolov8n_pose` 尚在准备中，本次未纳入重构范围。

## 目录结构

```text
.
├── 3rdparty/rknn/include/       # RKNN Toolkit2 2.3.2 C API 头文件
├── cmake/                       # RV1126B/aarch64 工具链
├── docs/                        # 仓库级 benchmark 报告
├── examples/<model>/
│   ├── cpp/                     # 原生 RKNN Runtime 应用
│   ├── model/                   # ONNX/RKNN 模型及元数据
│   ├── README.md                # 英文
│   └── README_CN.md             # 简体中文
├── install/rv1126b_linux_aarch64/ # 可直接复用的板端可执行文件
└── build-linux.sh               # 统一交叉构建入口
```

## 构建

```bash
export RECAMERA_SYSROOT=/absolute/path/to/recamera-pro-sysroot
export RECAMERA_RKNNRT="$RECAMERA_SYSROOT/usr/lib/librknnrt.so"
./build-linux.sh -d zipformer
./build-linux.sh -d mms_tts
./build-linux.sh -d yolo26n_depth
```

如需转换模型，必须使用 RKNN-Toolkit2 2.3.2，并设置 `target_platform='rv1126b'`。构建产物固定嵌入 `/userdata/rknn_test/3rdparty/lib` RUNPATH，不替换相机已有 Runtime。

实测数据见 [benchmark 报告](docs/benchmark_CN.md)。

## 上游参考

目录组织参考 [airockchip/rknn_model_zoo](https://github.com/airockchip/rknn_model_zoo)，但只保留 RV1126B Linux 所需内容。
