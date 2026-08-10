# RV1126B 语音 benchmark

简体中文 | [English](benchmark.md)

以下数据来自用户提供的 reCamera Pro 板端实测。它们是应用级端到端数据，不是孤立的 NPU 算子测试；只有在固件、RKNN Runtime、频率、温度、音频输入和播放配置相同的情况下才适合横向比较。

## MMS-TTS

请求文本：`hello world`；语速：`0.600`；生成音频：`0.736 s` / 11,776 个采样点。

| 阶段 | 耗时 |
| --- | ---: |
| 预处理 | 0.015 ms |
| Encoder | 133.166 ms |
| 中间处理 | 0.108 ms |
| Decoder | 493.426 ms |
| WAV 写入 | 5.284 ms |
| 合成 | 631.998 ms |
| 播放 | 782.649 ms |
| 请求总耗时 | 1,414.772 ms |

合成 RTF 为 **0.859**，即 **1.165 倍实时速度**。播放采用阻塞方式，因此计入请求总耗时，但不计入合成 RTF。

```json
{"request_id":9,"text":"hello world","speaking_rate":0.600,"audio_seconds":0.736,"samples":11776,"preprocess_ms":0.015,"encoder_ms":133.166,"middle_ms":0.108,"decoder_ms":493.426,"wav_write_ms":5.284,"synthesis_ms":631.998,"rtf":0.859,"x_realtime":1.165,"playback_ms":782.649,"playback_exit_code":0,"total_ms":1414.772,"wav_path":""}
```

## 流式 Zipformer STT

Runtime 初始化耗时：encoder **241.36 ms**、decoder **12.00 ms**、joiner **10.06 ms**。输入为 16 kHz 六通道音频，选择通道 0；encoder 算法窗口为 1,030 ms，步进 960 ms，lookahead 为 70 ms。

在日志中实际执行 encoder 的 14 个音频块上：

| 指标 | 实测值 |
| --- | ---: |
| 每块代表的音频时长 | 960 ms |
| Encoder 平均耗时 | 128.58 ms |
| Encoder 耗时范围 | 116.40–173.89 ms |
| Joiner 典型耗时（24 次调用） | 约 25–29 ms |
| 每块平均总计算耗时 | 161.92 ms |
| 平均单块 RTF | 0.169 |
| 最后一次累计 RTF | 0.158 |

该样本中的流式计算速度明显快于实时。第一块只执行 FBank，是因为 1,030 ms 算法窗口尚未填满。日志成功识别出 `哈喽你好` 和 `能听得到吗`；本次交互式测试没有测量 WER/CER 等准确率指标。
