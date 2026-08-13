# YOLO26n-Depth model

[简体中文](README_CN.md) | English

The verified `yolo26n-depth-rv1126b.rknn` model is stored here. SHA-256:

```text
b9318b9f71eb147ea784cd1e7b4272d5872250c33ac77403671004ede368b5a2
```

The application contract is RGB UINT8 NHWC `1x768x768` input with RKNN preprocessing and FLOAT32 NCHW `1x1x768x768` relative-depth output. `metadata.yaml` records Ultralytics 8.4.110, FP16 export (`quantize: 16`), and AGPL-3.0 licensing.

Metadata SHA-256: `30d704ec6f980b466dcdb2b43ad2e5c294f610b959b6ade6336ebce9c6f2819c`.
