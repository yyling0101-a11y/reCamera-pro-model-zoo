# 模型来源与契约

简体中文 | [English](README.md)

英文 MMS-TTS encoder 和 decoder 来自 Rockchip `rknn_model_zoo/examples/mms_tts`，并已转换为 RV1126B RKNN。ONNX 源模型与可直接运行的 RKNN 文件放在同一目录，校验值记录在 `SHA256SUMS`。

输入为最长 99 字符的英文 ASCII 文本，输出为 16 kHz 单声道 PCM 音频。在发布模型二进制前，请确认上游 `facebook/mms-tts-eng` 模型的再分发条款。
