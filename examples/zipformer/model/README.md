# Model provenance

[简体中文](README_CN.md) | English

Target: RV1126B; format: RKNN; source contract: Rockchip `rknn_model_zoo/examples/zipformer`; upstream model: `csukuangfj/k2fsa-zipformer-bilingual-zh-en-t`.

| File | SHA-256 |
| --- | --- |
| `encoder-epoch-99-avg-1-rv1126b.rknn` | `2d66e6bb2404c745b9af67c0caf90f8beee49590c078bce1da41e0d5c87a346b` |
| `decoder-epoch-99-avg-1-rv1126b.rknn` | `2a210c127f33c200a041749527e5b823a7fc29a450ba81c8ecaa93cf16ac720a` |
| `joiner-epoch-99-avg-1-rv1126b.rknn` | `031c54346c85d5776eaf8f578f1c9d5005af46fd8381d79aae667e987480b299` |

Audio is 16 kHz mono after channel selection. Encoder output is `24x512` plus persistent cache tensors; decoder uses a two-token context; joiner consumes two 512-element vectors and emits 6254 logits. Blank ID is 0 and unknown ID is 2.

