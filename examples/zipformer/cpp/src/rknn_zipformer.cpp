#include "rknn_zipformer.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "kaldi-native-fbank/csrc/online-feature.h"
#include "rknn_api.h"

namespace
{
constexpr int kSampleRate = 16000;
constexpr int kMelBins = 80;
constexpr int kSegmentFrames = 103;
constexpr int kFrameStep = 96;
constexpr int kEncoderFrames = 24;
constexpr int kEncoderDim = 512;
constexpr int kContextSize = 2;
constexpr int kJoinerVocab = 6254;
constexpr int kBlankId = 0;
constexpr int kUnkId = 2;

struct EncoderInputBuffer
{
  std::vector<float> floats;
  std::vector<int64_t> integers;

  void initialize(const rknn_tensor_attr &attr)
  {
    if (attr.type == RKNN_TENSOR_INT64)
      integers.assign(attr.n_elems, 0);
    else
      floats.assign(attr.n_elems, 0.0f);
  }
  void *data()
  {
    return integers.empty() ? static_cast<void *>(floats.data())
                            : static_cast<void *>(integers.data());
  }
  uint32_t bytes() const
  {
    return static_cast<uint32_t>(integers.empty()
                                     ? floats.size() * sizeof(float)
                                     : integers.size() * sizeof(int64_t));
  }
  rknn_tensor_type runtime_type() const
  {
    return integers.empty() ? RKNN_TENSOR_FLOAT32 : RKNN_TENSOR_INT64;
  }
};

using Clock = std::chrono::steady_clock;
double elapsed_ms(Clock::time_point start)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}

std::string dims(const rknn_tensor_attr &attr)
{
  std::ostringstream out;
  for (uint32_t i = 0; i < attr.n_dims; ++i)
  {
    if (i) out << 'x';
    out << attr.dims[i];
  }
  return out.str();
}

std::vector<std::string> read_vocab(const std::string &path)
{
  std::ifstream file(path);
  if (!file) throw std::runtime_error("无法打开 Zipformer 词表: " + path);
  std::vector<std::string> vocab;
  std::string line;
  while (std::getline(file, line))
  {
    const auto split = line.rfind(' ');
    if (split == std::string::npos) continue;
    const int id = std::stoi(line.substr(split + 1));
    if (id < 0) continue;
    if (vocab.size() <= static_cast<size_t>(id))
      vocab.resize(static_cast<size_t>(id) + 1);
    vocab[static_cast<size_t>(id)] = line.substr(0, split);
  }
  if (vocab.size() < static_cast<size_t>(kJoinerVocab))
    throw std::runtime_error("Zipformer 词表不完整: " + path);
  return vocab;
}

void sentencepiece_space(std::string &token)
{
  const std::string marker = "▁";
  size_t pos = 0;
  while ((pos = token.find(marker, pos)) != std::string::npos)
  {
    token.replace(pos, marker.size(), " ");
    ++pos;
  }
}

class RknnModel
{
  public:
  RknnModel(std::string label, const std::string &path)
      : label_(std::move(label)), path_(path)
  {
    const auto start = Clock::now();
    check(rknn_init(&ctx_, const_cast<char *>(path.c_str()), 0, 0, nullptr),
          "rknn_init");
    rknn_input_output_num count{};
    check(rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &count, sizeof(count)),
          "查询 IO 数量");
    inputs_.resize(count.n_input);
    outputs_.resize(count.n_output);
    for (uint32_t i = 0; i < count.n_input; ++i)
    {
      inputs_[i].index = i;
      check(rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &inputs_[i],
                       sizeof(inputs_[i])),
            "查询输入");
    }
    for (uint32_t i = 0; i < count.n_output; ++i)
    {
      outputs_[i].index = i;
      check(rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &outputs_[i],
                       sizeof(outputs_[i])),
            "查询输出");
    }
    std::cerr << "[RKNN] " << label_ << " init=" << std::fixed
              << std::setprecision(2) << elapsed_ms(start)
              << " ms, inputs=" << count.n_input
              << ", outputs=" << count.n_output << '\n';
    for (const auto &a : inputs_)
      std::cerr << "  input[" << a.index << "] " << a.name << " [" << dims(a)
                << "] elems=" << a.n_elems << " type=" << a.type
                << " fmt=" << a.fmt << '\n';
    for (const auto &a : outputs_)
      std::cerr << "  output[" << a.index << "] " << a.name << " [" << dims(a)
                << "] elems=" << a.n_elems << " type=" << a.type
                << " fmt=" << a.fmt << '\n';
  }
  ~RknnModel()
  {
    if (ctx_) rknn_destroy(ctx_);
  }
  RknnModel(const RknnModel &) = delete;
  RknnModel &operator=(const RknnModel &) = delete;

  rknn_context ctx() const { return ctx_; }
  const std::vector<rknn_tensor_attr> &inputs() const { return inputs_; }
  const std::vector<rknn_tensor_attr> &outputs() const { return outputs_; }
  void check(int ret, const char *operation) const
  {
    if (ret != RKNN_SUCC)
      throw std::runtime_error(label_ + " " + operation + " 失败 (ret=" +
                               std::to_string(ret) + "): " + path_);
  }

  private:
  std::string label_;
  std::string path_;
  rknn_context ctx_ = 0;
  std::vector<rknn_tensor_attr> inputs_;
  std::vector<rknn_tensor_attr> outputs_;
};

void nchw_to_nhwc(const float *src, float *dst, const rknn_tensor_attr &attr)
{
  if (attr.n_dims != 4)
    throw std::runtime_error("encoder cache NHWC 张量不是四维");
  const int n = static_cast<int>(attr.dims[0]);
  const int h = static_cast<int>(attr.dims[1]);
  const int w = static_cast<int>(attr.dims[2]);
  const int c = static_cast<int>(attr.dims[3]);
  for (int ni = 0; ni < n; ++ni)
    for (int ci = 0; ci < c; ++ci)
      for (int hi = 0; hi < h; ++hi)
        for (int wi = 0; wi < w; ++wi)
          dst[((ni * h + hi) * w + wi) * c + ci] =
              src[((ni * c + ci) * h + hi) * w + wi];
}
}  // namespace

struct RknnZipformer::Impl
{
  Impl(const std::string &encoder_path, const std::string &decoder_path,
       const std::string &joiner_path, const std::string &vocab_path)
      : encoder("encoder", encoder_path),
        decoder("decoder", decoder_path),
        joiner("joiner", joiner_path),
        vocab(read_vocab(vocab_path)),
        fbank(make_fbank_options())
  {
    validate_contract();
    encoder_inputs.resize(encoder.inputs().size());
    for (size_t i = 0; i < encoder_inputs.size(); ++i)
      encoder_inputs[i].initialize(encoder.inputs()[i]);
    decoder_tokens.assign(kContextSize, 0);
    run_decoder();
    std::cerr << "[stream] ready: 16 kHz mono, chunk-step=" << kFrameStep * 10
              << " ms, lookahead=" << (kSegmentFrames - kFrameStep) * 10
              << " ms, algorithmic-window=" << kSegmentFrames * 10 << " ms\n";
  }

  static knf::FbankOptions make_fbank_options()
  {
    knf::FbankOptions o;
    o.frame_opts.samp_freq = kSampleRate;
    o.mel_opts.num_bins = kMelBins;
    o.mel_opts.high_freq = -400;
    o.frame_opts.dither = 0;
    o.frame_opts.snip_edges = false;
    return o;
  }

  void validate_contract()
  {
    if (encoder.inputs().empty() ||
        encoder.outputs().size() != encoder.inputs().size() ||
        encoder.inputs()[0].n_elems != kSegmentFrames * kMelBins ||
        encoder.outputs()[0].n_elems != kEncoderFrames * kEncoderDim)
      throw std::runtime_error(
          "encoder IO 与 streaming Zipformer 103x80 -> 24x512/cache "
          "契约不匹配");
    for (size_t i = 1; i < encoder.inputs().size(); ++i)
      if (encoder.inputs()[i].n_elems != encoder.outputs()[i].n_elems ||
          encoder.inputs()[i].type != encoder.outputs()[i].type)
        throw std::runtime_error(
            "encoder cache input/output 元素数不匹配，索引 " +
            std::to_string(i));
    if (decoder.inputs().size() != 1 || decoder.outputs().size() != 1 ||
        decoder.inputs()[0].n_elems != kContextSize ||
        decoder.outputs()[0].n_elems != kEncoderDim)
      throw std::runtime_error("decoder IO 与 Zipformer 2 -> 512 契约不匹配");
    if (joiner.inputs().size() != 2 || joiner.outputs().size() != 1 ||
        joiner.inputs()[0].n_elems != kEncoderDim ||
        joiner.inputs()[1].n_elems != kEncoderDim ||
        joiner.outputs()[0].n_elems != kJoinerVocab)
      throw std::runtime_error(
          "joiner IO 与 Zipformer 512+512 -> 6254 契约不匹配");
  }

  double run_decoder()
  {
    rknn_input input{};
    input.index = 0;
    input.type = RKNN_TENSOR_INT64;
    input.fmt = decoder.inputs()[0].fmt;
    input.size = static_cast<uint32_t>(decoder_tokens.size() * sizeof(int64_t));
    input.buf = decoder_tokens.data();
    const auto start = Clock::now();
    decoder.check(rknn_inputs_set(decoder.ctx(), 1, &input), "设置输入");
    decoder.check(rknn_run(decoder.ctx(), nullptr), "运行");
    rknn_output output{};
    output.index = 0;
    output.want_float = 1;
    decoder.check(rknn_outputs_get(decoder.ctx(), 1, &output, nullptr),
                  "读取输出");
    decoder_state.resize(kEncoderDim);
    std::memcpy(decoder_state.data(), output.buf,
                decoder_state.size() * sizeof(float));
    decoder.check(rknn_outputs_release(decoder.ctx(), 1, &output), "释放输出");
    return elapsed_ms(start);
  }

  double run_joiner(const float *encoder_frame, int &token)
  {
    rknn_input inputs[2]{};
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_FLOAT32;
    inputs[0].fmt = joiner.inputs()[0].fmt;
    inputs[0].size = kEncoderDim * sizeof(float);
    inputs[0].buf = const_cast<float *>(encoder_frame);
    inputs[1].index = 1;
    inputs[1].type = RKNN_TENSOR_FLOAT32;
    inputs[1].fmt = joiner.inputs()[1].fmt;
    inputs[1].size = kEncoderDim * sizeof(float);
    inputs[1].buf = decoder_state.data();
    const auto start = Clock::now();
    joiner.check(rknn_inputs_set(joiner.ctx(), 2, inputs), "设置输入");
    joiner.check(rknn_run(joiner.ctx(), nullptr), "运行");
    rknn_output output{};
    output.index = 0;
    output.want_float = 1;
    joiner.check(rknn_outputs_get(joiner.ctx(), 1, &output, nullptr),
                 "读取输出");
    const float *scores = static_cast<const float *>(output.buf);
    token = static_cast<int>(std::max_element(scores, scores + kJoinerVocab) -
                             scores);
    joiner.check(rknn_outputs_release(joiner.ctx(), 1, &output), "释放输出");
    return elapsed_ms(start);
  }

  double run_encoder(std::vector<float> &encoded)
  {
    std::vector<rknn_input> inputs(encoder_inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i)
    {
      inputs[i].index = static_cast<uint32_t>(i);
      inputs[i].type = encoder_inputs[i].runtime_type();
      inputs[i].fmt = encoder.inputs()[i].fmt;
      inputs[i].size = encoder_inputs[i].bytes();
      inputs[i].buf = encoder_inputs[i].data();
    }
    const auto start = Clock::now();
    encoder.check(
        rknn_inputs_set(encoder.ctx(), static_cast<uint32_t>(inputs.size()),
                        inputs.data()),
        "设置输入");
    encoder.check(rknn_run(encoder.ctx(), nullptr), "运行");
    std::vector<rknn_output> outputs(encoder.outputs().size());
    for (size_t i = 0; i < outputs.size(); ++i)
    {
      outputs[i].index = static_cast<uint32_t>(i);
      outputs[i].want_float =
          encoder.outputs()[i].type == RKNN_TENSOR_INT64 ? 0 : 1;
    }
    encoder.check(
        rknn_outputs_get(encoder.ctx(), static_cast<uint32_t>(outputs.size()),
                         outputs.data(), nullptr),
        "读取输出");
    encoded.resize(kEncoderFrames * kEncoderDim);
    std::memcpy(encoded.data(), outputs[0].buf, encoded.size() * sizeof(float));
    for (size_t i = 1; i < outputs.size(); ++i)
    {
      if (encoder.inputs()[i].type == RKNN_TENSOR_INT64)
      {
        std::memcpy(encoder_inputs[i].integers.data(), outputs[i].buf,
                    encoder_inputs[i].integers.size() * sizeof(int64_t));
      }
      else if (encoder.inputs()[i].fmt == RKNN_TENSOR_NHWC)
      {
        nchw_to_nhwc(static_cast<const float *>(outputs[i].buf),
                     encoder_inputs[i].floats.data(), encoder.inputs()[i]);
      }
      else
      {
        std::memcpy(encoder_inputs[i].floats.data(), outputs[i].buf,
                    encoder_inputs[i].floats.size() * sizeof(float));
      }
    }
    encoder.check(rknn_outputs_release(encoder.ctx(),
                                       static_cast<uint32_t>(outputs.size()),
                                       outputs.data()),
                  "释放输出");
    return elapsed_ms(start);
  }

  std::string accept(const std::vector<float> &audio)
  {
    const auto call_start = Clock::now();
    const auto fbank_start = Clock::now();
    if (!audio.empty())
      fbank.AcceptWaveform(kSampleRate, audio.data(),
                           static_cast<int32_t>(audio.size()));
    const double fbank_ms = elapsed_ms(fbank_start);
    std::string emitted;
    int blocks = 0, tokens = 0, joiner_calls = 0, decoder_calls = 0;
    double encoder_ms = 0, decoder_ms = 0, joiner_ms = 0;
    while (fbank.NumFramesReady() >= processed_frames + kSegmentFrames)
    {
      for (int frame = 0; frame < kSegmentFrames; ++frame)
      {
        const float *feature = fbank.GetFrame(processed_frames + frame);
        std::memcpy(encoder_inputs[0].floats.data() + frame * kMelBins, feature,
                    kMelBins * sizeof(float));
      }
      std::vector<float> encoded;
      encoder_ms += run_encoder(encoded);
      ++blocks;
      for (int frame = 0; frame < kEncoderFrames; ++frame)
      {
        int token = kBlankId;
        joiner_ms += run_joiner(encoded.data() + frame * kEncoderDim, token);
        ++joiner_calls;
        if (token == kBlankId || token == kUnkId) continue;
        decoder_tokens[0] = decoder_tokens[1];
        decoder_tokens[1] = token;
        if (token >= 0 && static_cast<size_t>(token) < vocab.size())
        {
          std::string piece = vocab[static_cast<size_t>(token)];
          sentencepiece_space(piece);
          emitted += piece;
        }
        decoder_ms += run_decoder();
        ++decoder_calls;
        ++tokens;
      }
      processed_frames += kFrameStep;
    }
    const double total_ms = elapsed_ms(call_start);
    total_audio_samples += audio.size();
    total_compute_ms += total_ms;
    const double block_audio_ms = blocks * kFrameStep * 10.0;
    const double rtf = block_audio_ms > 0 ? total_ms / block_audio_ms : 0.0;
    const double cumulative_audio_s =
        static_cast<double>(total_audio_samples) / kSampleRate;
    std::cerr << std::fixed << std::setprecision(2)
              << "[perf] audio=" << audio.size() * 1000.0 / kSampleRate << " ms"
              << " blocks=" << blocks << " fbank=" << fbank_ms << " ms"
              << " encoder=" << encoder_ms << " ms"
              << " decoder=" << decoder_ms << " ms/" << decoder_calls
              << " joiner=" << joiner_ms << " ms/" << joiner_calls
              << " total=" << total_ms << " ms rtf=" << std::setprecision(3)
              << rtf << " tokens=" << tokens << " cumulative_rtf="
              << (cumulative_audio_s > 0
                      ? total_compute_ms / (cumulative_audio_s * 1000.0)
                      : 0.0)
              << '\n';
    return emitted;
  }

  RknnModel encoder, decoder, joiner;
  std::vector<std::string> vocab;
  knf::OnlineFbank fbank;
  std::vector<EncoderInputBuffer> encoder_inputs;
  std::vector<int64_t> decoder_tokens;
  std::vector<float> decoder_state;
  int processed_frames = 0;
  uint64_t total_audio_samples = 0;
  double total_compute_ms = 0;
};

RknnZipformer::RknnZipformer(const std::string &encoder_path,
                             const std::string &decoder_path,
                             const std::string &joiner_path,
                             const std::string &vocab_path)
    : impl_(std::make_unique<Impl>(encoder_path, decoder_path, joiner_path,
                                   vocab_path))
{
}
RknnZipformer::~RknnZipformer() = default;
std::string RknnZipformer::accept_waveform(const std::vector<float> &audio)
{
  return impl_->accept(audio);
}
