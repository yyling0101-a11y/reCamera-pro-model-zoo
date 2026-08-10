# 模型来源与契约

简体中文 | [English](README.md)

目标平台：RV1126B；格式：RKNN；部署契约来源：Rockchip `rknn_model_zoo/examples/zipformer`；上游模型：`csukuangfj/k2fsa-zipformer-bilingual-zh-en-t`。

| 文件 | SHA-256 |
| --- | --- |
| `encoder-epoch-99-avg-1-rv1126b.rknn` | `2d66e6bb2404c745b9af67c0caf90f8beee49590c078bce1da41e0d5c87a346b` |
| `decoder-epoch-99-avg-1-rv1126b.rknn` | `2a210c127f33c200a041749527e5b823a7fc29a450ba81c8ecaa93cf16ac720a` |
| `joiner-epoch-99-avg-1-rv1126b.rknn` | `031c54346c85d5776eaf8f578f1c9d5005af46fd8381d79aae667e987480b299` |

选择通道后输入为 16 kHz 单声道音频。Encoder 输出为 `24x512` 及持久 cache 张量；decoder 使用两个 token 的上下文；joiner 接收两个 512 元素向量并输出 6254 个 logit。Blank ID 为 0，unknown ID 为 2。

