# Streaming Zipformer STT

[简体中文](README_CN.md) | English

C++17 streaming Chinese-English speech recognition for reCamera Pro (RV1126B/aarch64). It captures `S16_LE`, 16 kHz, six-channel audio from ALSA `hw:0,0`, selects channel 0 by default, computes 80-bin Kaldi FBank features, and runs the RKNN encoder, decoder, and joiner on the NPU while preserving streaming state.

## Models

The four files in `model/` form one inseparable model contract:

- `encoder-epoch-99-avg-1-rv1126b.rknn`
- `decoder-epoch-99-avg-1-rv1126b.rknn`
- `joiner-epoch-99-avg-1-rv1126b.rknn`
- `vocab.txt`

The model originates from `csukuangfj/k2fsa-zipformer-bilingual-zh-en-t` through Rockchip's `rknn_model_zoo/examples/zipformer`. Input is 80-bin FBank with 103 frames per encoder call and a 96-frame (960 ms) step. Do not mix the vocabulary or any component with another export.

## Build and run

From the repository root:

```bash
export RECAMERA_SYSROOT=/absolute/path/to/recamera-pro-sysroot
./build-linux.sh -d zipformer
```

Copy `install/rv1126b_linux_aarch64/rknn_zipformer_demo/recamera_stt` and this example's `model/` directory to the camera, then run from their common parent:

```bash
export LD_LIBRARY_PATH=/userdata/rknn_test/3rdparty/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
./recamera_stt --encoder model/encoder-epoch-99-avg-1-rv1126b.rknn \
  --decoder model/decoder-epoch-99-avg-1-rv1126b.rknn \
  --joiner model/joiner-epoch-99-avg-1-rv1126b.rknn \
  --vocab model/vocab.txt --channel 0
```

Use `--channel -1` to average all six channels. A per-block `rtf < 1` means inference is faster than real time. See the [measured results](../../docs/benchmark.md#streaming-zipformer-stt).

