# YOLO26n-Depth RTSP

[简体中文](README_CN.md) | English

Native C++ monocular-depth application for reCamera Pro (RV1126B/aarch64). It captures the camera from `/dev/video13`, letterboxes RGB frames to `768x768`, runs the depth RKNN model, overlays a Turbo heatmap, encodes the processed RGB frame as JPEG, and serves MJPEG/RTP through an embedded RTSP server. No external MediaMTX relay is required.

The output is a relative/log-depth response rather than metric distance. Each frame maps its 2nd–98th depth percentiles to the colour range. The default `alpha=0.65` preserves scene detail; `alpha=1` outputs only the heatmap.

## Model contract

- Input: RGB UINT8, NHWC `1x768x768`.
- Preprocessing: aspect-preserving letterbox with value 114; RKNN `/255` normalization.
- Output: FLOAT32/NCHW `1x1x768x768` relative depth.
- Expected model: `model/yolo26n-depth-rv1126b.rknn`.
- Expected SHA-256: `b9318b9f71eb147ea784cd1e7b4272d5872250c33ac77403671004ede368b5a2`.

The verified model and its export metadata are stored in `model/`. The supplied metadata identifies Ultralytics 8.4.110, task `depth`, 768×768 input, FP16 export (`quantize: 16`), and AGPL-3.0 licensing. Review those license terms before redistribution or commercial use.

## Build

The sysroot must contain target aarch64 GStreamer/GLib libraries and pkg-config metadata. The example also carries the RTSP Server development headers needed by this build.

```bash
export RECAMERA_SYSROOT=/absolute/path/to/recamera-pro-sysroot
export RECAMERA_CROSS_PREFIX=/absolute/path/to/bin/aarch64-linux-gnu-
./build-linux.sh -d yolo26n_depth
```

## Run

Copy the executable from `install/rv1126b_linux_aarch64/rknn_yolo26n_depth_demo/` and the model to the camera:

```bash
gst-inspect-1.0 v4l2src videoconvert videoscale appsrc appsink jpegenc rtpjpegpay
./recamera_depth_rtsp model/yolo26n-depth-rv1126b.rknn \
  8554 /recamera-depth 1280 720 30 0.65
```

Arguments after the model are `port mount width height fps alpha` and are optional. View the stream from another device:

```bash
ffplay -rtsp_transport tcp rtsp://192.168.42.1:8554/recamera-depth
```

The configured 30 FPS is the stream caps target; actual output is bounded by inference and postprocessing throughput. See the [RV1126B benchmark](../../docs/benchmark.md#yolo26n-depth).
