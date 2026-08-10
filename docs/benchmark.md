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

