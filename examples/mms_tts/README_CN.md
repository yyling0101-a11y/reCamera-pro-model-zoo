# MMS-TTS HTTP benchmark

简体中文 | [English](README.md)

面向 reCamera Pro（RV1126B/aarch64）的常驻 C++ RKNN Runtime HTTP 服务。它接收最多 99 个英文 ASCII 字符，通过 MMS-TTS encoder 和 decoder 合成 16 kHz 单声道 PCM WAV，并可通过 ALSA 播放。

## 构建与运行

```bash
export RECAMERA_SYSROOT=/absolute/path/to/recamera-pro-sysroot
./build-linux.sh -d mms_tts
```

把 `install/rv1126b_linux_aarch64/rknn_mms_tts_demo/` 中的可执行文件和本示例的 `model/` 目录传到相机：

```bash
./recamera_tts_benchmark \
  model/mms_tts_eng_encoder_200_rv1126b.rknn \
  model/mms_tts_eng_decoder_200_rv1126b.rknn \
  --port 8080 --speaking-rate 0.8
```

语速范围为 0.5–1.5，数值越小越慢。可按需使用 `--device hw:0,0`、`--keep-wav` 或 `--no-play`。

```bash
curl -s http://DEVICE_IP:8080/health
curl -s -X POST http://DEVICE_IP:8080/tts \
  -H 'Content-Type: application/json' -d '{"text":"hello world"}'
```

Windows CMD 需要转义双引号：

```bat
curl -s -X POST http://192.168.42.1:8080/tts -H "Content-Type: application/json" -d "{\"text\":\"hello world\"}"
```

为保证 RKNN context 和播放安全，请求会串行执行。每个响应都包含各阶段耗时、RTF、播放时间和总延迟。实测数据见 [benchmark 报告](../../docs/benchmark_CN.md#mms-tts)。

