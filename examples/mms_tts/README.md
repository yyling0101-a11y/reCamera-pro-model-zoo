# MMS-TTS HTTP benchmark

[简体中文](README_CN.md) | English

A resident C++ RKNN Runtime HTTP service for reCamera Pro (RV1126B/aarch64). It accepts up to 99 English ASCII characters, synthesizes 16 kHz mono PCM WAV with the MMS-TTS encoder and decoder, and can play the result through ALSA.

## Build and run

```bash
export RECAMERA_SYSROOT=/absolute/path/to/recamera-pro-sysroot
./build-linux.sh -d mms_tts
```

Copy the executable from `install/rv1126b_linux_aarch64/rknn_mms_tts_demo/` and this example's `model/` directory to the camera:

```bash
./recamera_tts_benchmark \
  model/mms_tts_eng_encoder_200_rv1126b.rknn \
  model/mms_tts_eng_decoder_200_rv1126b.rknn \
  --port 8080 --speaking-rate 0.8
```

The speaking-rate range is 0.5–1.5; smaller values are slower. Use `--device hw:0,0`, `--keep-wav`, or `--no-play` as needed.

```bash
curl -s http://DEVICE_IP:8080/health
curl -s -X POST http://DEVICE_IP:8080/tts \
  -H 'Content-Type: application/json' -d '{"text":"hello world"}'
```

Windows CMD requires escaped double quotes:

```bat
curl -s -X POST http://192.168.42.1:8080/tts -H "Content-Type: application/json" -d "{\"text\":\"hello world\"}"
```

Requests are serialized for RKNN context and playback safety. Each response includes stage timings, RTF, playback time, and total latency. See the [measured results](../../docs/benchmark.md#mms-tts).

