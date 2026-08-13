# RV1126B speech benchmarks

[简体中文](benchmark_CN.md) | English

These are observed end-to-end application measurements supplied from a reCamera Pro run. They are not isolated NPU operator benchmarks and should be compared only under equivalent firmware, RKNN Runtime, clock, thermal, audio, and playback conditions.

## MMS-TTS

Request: `hello world`; speaking rate: `0.600`; generated audio: `0.736 s` / 11,776 samples.

| Stage | Time |
| --- | ---: |
| Preprocess | 0.015 ms |
| Encoder | 133.166 ms |
| Intermediate processing | 0.108 ms |
| Decoder | 493.426 ms |
| WAV write | 5.284 ms |
| Synthesis | 631.998 ms |
| Playback | 782.649 ms |
| Total request | 1,414.772 ms |

Synthesis RTF was **0.859**, or **1.165× real time**. Playback is deliberately blocking and is included in total request time, but excluded from synthesis RTF.

```json
{"request_id":9,"text":"hello world","speaking_rate":0.600,"audio_seconds":0.736,"samples":11776,"preprocess_ms":0.015,"encoder_ms":133.166,"middle_ms":0.108,"decoder_ms":493.426,"wav_write_ms":5.284,"synthesis_ms":631.998,"rtf":0.859,"x_realtime":1.165,"playback_ms":782.649,"playback_exit_code":0,"total_ms":1414.772,"wav_path":""}
```

## Streaming Zipformer STT

Runtime initialization: encoder **241.36 ms**, decoder **12.00 ms**, joiner **10.06 ms**. Audio was 16 kHz, six channels with channel 0 selected; the encoder's algorithmic window was 1,030 ms with a 960 ms step and 70 ms lookahead.

Across the 14 reported blocks that performed encoder inference:

| Metric | Observed value |
| --- | ---: |
| Audio represented per block | 960 ms |
| Mean encoder time | 128.58 ms |
| Encoder range | 116.40–173.89 ms |
| Typical joiner time (24 calls) | about 25–29 ms |
| Mean total compute per block | 161.92 ms |
| Mean block RTF | 0.169 |
| Last reported cumulative RTF | 0.158 |

The stream therefore had substantial compute headroom relative to real time in this sample. The first block contained only FBank work because the 1,030 ms algorithmic window had not filled. Recognized utterances included `哈喽你好` and `能听得到吗`. Accuracy metrics such as WER/CER were not measured by this interactive log.

## YOLO26n-Depth

Command:

```bash
./recamera_depth_rtsp ./yolo26n-depth-rv1126b.rknn 8554 /recamera-depth
```

The model reported RGB UINT8/NHWC input `1x768x768x3` and FLOAT32/NCHW output `1x1x768x768`. The supplied log contains 93 frames (`0–92`); an RTSP client connected before frame 11. Frame 0 is retained as a cold-start observation rather than mixed silently into steady-state interpretation.

| Metric | All observed frames | RTSP connected, frames 11–92 |
| --- | ---: | ---: |
| Preprocess | 32.55–35.26 ms | 32.55–35.26 ms |
| NPU run | 239.07–263.17 ms | 239.07–252.48 ms |
| RKNN output get | 5.79–8.14 ms | 5.79–8.00 ms |
| Inference total | 277.94–304.92 ms | 277.94–291.82 ms |
| Depth visualization postprocess | 110.24–116.84 ms | 110.24–116.84 ms |
| End-to-end frame total | 392.60–419.93 ms | 392.60–408.50 ms |

Once the client was connected, end-to-end throughput was approximately **2.45–2.55 FPS**, typically about **2.5 FPS**. This includes capture-to-app processing, letterbox preprocessing, NPU execution, output retrieval, percentile calculation, Turbo overlay, and submission to the RTSP pipeline. It does not isolate JPEG encoding or network/display latency at the viewer.

The `depth_p02`/`depth_p98` values are per-frame visualization percentiles. Their observed variation must not be interpreted as metric distance because the model contract does not define a physical unit. No depth-accuracy dataset metric was measured in this run.
