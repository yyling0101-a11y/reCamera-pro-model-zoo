# 流式 Zipformer 语音转文字

简体中文 | [English](README.md)

面向 reCamera Pro（RV1126B/aarch64）的 C++17 流式中英双语识别程序。程序从 ALSA `hw:0,0` 采集 `S16_LE`、16 kHz、六通道音频，默认选择通道 0，计算 80 维 Kaldi FBank，并在 NPU 上运行 RKNN encoder、decoder 和 joiner，同时跨音频块保留流式状态。

## 模型

`model/` 中以下四个文件共同构成不可拆分的模型契约：

- `encoder-epoch-99-avg-1-rv1126b.rknn`
- `decoder-epoch-99-avg-1-rv1126b.rknn`
- `joiner-epoch-99-avg-1-rv1126b.rknn`
- `vocab.txt`

模型源自 `csukuangfj/k2fsa-zipformer-bilingual-zh-en-t`，部署契约参考 Rockchip `rknn_model_zoo/examples/zipformer`。输入为 80 维 FBank，每次 encoder 使用 103 帧，步进 96 帧（960 ms）。不要混用其他导出版本的词表或任一模型组件。

## 构建与运行

在仓库根目录执行：

```bash
export RECAMERA_SYSROOT=/absolute/path/to/recamera-pro-sysroot
./build-linux.sh -d zipformer
```

把 `install/rv1126b_linux_aarch64/rknn_zipformer_demo/recamera_stt` 和本示例的 `model/` 目录传到相机，并在它们的共同父目录运行：

```bash
export LD_LIBRARY_PATH=/userdata/rknn_test/3rdparty/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
./recamera_stt --encoder model/encoder-epoch-99-avg-1-rv1126b.rknn \
  --decoder model/decoder-epoch-99-avg-1-rv1126b.rknn \
  --joiner model/joiner-epoch-99-avg-1-rv1126b.rknn \
  --vocab model/vocab.txt --channel 0
```

`--channel -1` 表示对六通道做平均下混。单块 `rtf < 1` 表示推理速度快于实时。实测数据见 [benchmark 报告](../../docs/benchmark_CN.md#流式-zipformer-stt)。

