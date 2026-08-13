# YOLO26n-Depth RTSP

简体中文 | [English](README.md)

面向 reCamera Pro（RV1126B/aarch64）的原生 C++ 单目深度估计应用。程序从 `/dev/video13` 采集图像，将 RGB 图像等比例 letterbox 到 `768x768`，运行深度 RKNN 模型，叠加 Turbo 热力图，再把处理后的 RGB 图像编码为 JPEG，并通过内嵌 RTSP Server 输出 MJPEG/RTP，不需要外部 MediaMTX 中继。

模型输出是相对深度或 log-depth 响应，不是带有米制单位的绝对距离。每帧使用深度值的第 2–98 百分位映射颜色。默认 `alpha=0.65` 保留场景细节，`alpha=1` 输出纯热力图。

## 模型契约

- 输入：RGB UINT8，NHWC `1x768x768`。
- 预处理：保持宽高比的 letterbox，填充值 114；RKNN 内执行 `/255` 归一化。
- 输出：FLOAT32/NCHW `1x1x768x768` 相对深度。
- 预期模型：`model/yolo26n-depth-rv1126b.rknn`。
- 预期 SHA-256：`b9318b9f71eb147ea784cd1e7b4272d5872250c33ac77403671004ede368b5a2`。

经过校验的模型及其导出元数据已存放在 `model/`。元数据显示该模型使用 Ultralytics 8.4.110、任务为 `depth`、输入尺寸为 768×768、采用 FP16 导出（`quantize: 16`），许可证为 AGPL-3.0。再分发或商用前请核实并遵守对应许可条款。

## 构建

Sysroot 必须包含目标端 aarch64 GStreamer/GLib 库和 pkg-config 元数据。本示例同时保留构建所需的 RTSP Server 开发头文件。

```bash
export RECAMERA_SYSROOT=/absolute/path/to/recamera-pro-sysroot
export RECAMERA_CROSS_PREFIX=/absolute/path/to/bin/aarch64-linux-gnu-
./build-linux.sh -d yolo26n_depth
```

## 运行

把 `install/rv1126b_linux_aarch64/rknn_yolo26n_depth_demo/` 中的可执行文件及模型传到相机：

```bash
gst-inspect-1.0 v4l2src videoconvert videoscale appsrc appsink jpegenc rtpjpegpay
./recamera_depth_rtsp model/yolo26n-depth-rv1126b.rknn \
  8554 /recamera-depth 1280 720 30 0.65
```

模型之后的可选参数依次为 `port mount width height fps alpha`。从其他设备观看：

```bash
ffplay -rtsp_transport tcp rtsp://192.168.42.1:8554/recamera-depth
```

配置的 30 FPS 是 RTSP caps 目标，实际输出帧率受推理和后处理吞吐限制。实测结果见 [RV1126B benchmark](../../docs/benchmark_CN.md#yolo26n-depth)。
