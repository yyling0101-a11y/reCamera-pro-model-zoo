# YOLO26n-Depth 模型

简体中文 | [English](README.md)

经过校验的 `yolo26n-depth-rv1126b.rknn` 模型已存放在此目录。SHA-256：

```text
b9318b9f71eb147ea784cd1e7b4272d5872250c33ac77403671004ede368b5a2
```

应用运行契约为 RGB UINT8 NHWC `1x768x768` 输入、RKNN 内部预处理，以及 FLOAT32 NCHW `1x1x768x768` 相对深度输出。`metadata.yaml` 记录了 Ultralytics 8.4.110、FP16 导出（`quantize: 16`）和 AGPL-3.0 许可信息。

元数据 SHA-256：`30d704ec6f980b466dcdb2b43ad2e5c294f610b959b6ade6336ebce9c6f2819c`。
