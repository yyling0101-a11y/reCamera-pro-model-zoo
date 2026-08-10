#include "tts_engine.h"

#include <rknn_api.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;
constexpr int kMaxLength = 200;
constexpr int kPredictedMax = 400;
constexpr int kHopLength = 256;
constexpr int kPriorChannels = 192;

double elapsed_ms(Clock::time_point begin, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

void require_rknn(int code, const char* operation) {
  if (code != RKNN_SUCC)
    throw std::runtime_error(std::string(operation) +
                             " failed: " + std::to_string(code));
}

std::vector<uint8_t> read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) throw std::runtime_error("cannot open model: " + path);
  const auto size = file.tellg();
  if (size <= 0) throw std::runtime_error("empty model: " + path);
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!file) throw std::runtime_error("cannot read model: " + path);
  return bytes;
}

class Model {
 public:
  explicit Model(const std::string& path) : bytes_(read_file(path)) {
    require_rknn(rknn_init(&context_, bytes_.data(),
                           static_cast<uint32_t>(bytes_.size()), 0, nullptr),
                 "rknn_init");
    require_rknn(rknn_query(context_, RKNN_QUERY_IN_OUT_NUM, &io_, sizeof(io_)),
                 "query io count");
  }
  ~Model() { if (context_) rknn_destroy(context_); }
  Model(const Model&) = delete;
  Model& operator=(const Model&) = delete;

  rknn_context context() const { return context_; }
  const rknn_input_output_num& io() const { return io_; }

  std::string summary(const std::string& label) const {
    std::ostringstream text;
    text << label << " inputs=" << io_.n_input << " outputs=" << io_.n_output;
    for (uint32_t i = 0; i < io_.n_input; ++i) {
      rknn_tensor_attr attr{};
      attr.index = i;
      require_rknn(rknn_query(context_, RKNN_QUERY_INPUT_ATTR, &attr,
                              sizeof(attr)), "query input");
      text << "\n  in[" << i << "] " << attr.name << " elems=" << attr.n_elems
           << " type=" << attr.type;
    }
    for (uint32_t i = 0; i < io_.n_output; ++i) {
      rknn_tensor_attr attr{};
      attr.index = i;
      require_rknn(rknn_query(context_, RKNN_QUERY_OUTPUT_ATTR, &attr,
                              sizeof(attr)), "query output");
      text << "\n  out[" << i << "] " << attr.name << " elems=" << attr.n_elems
           << " type=" << attr.type;
    }
    return text.str();
  }

 private:
  rknn_context context_ = 0;
  std::vector<uint8_t> bytes_;
  rknn_input_output_num io_{};
};

const std::map<char, int64_t> kVocab = {
    {' ', 19}, {'\'', 1}, {'-', 14}, {'0', 23}, {'1', 15}, {'2', 28},
    {'3', 11}, {'4', 27}, {'5', 35}, {'6', 36}, {'_', 30}, {'a', 26},
    {'b', 24}, {'c', 12}, {'d', 5}, {'e', 7}, {'f', 20}, {'g', 37},
    {'h', 6}, {'i', 18}, {'j', 16}, {'k', 0}, {'l', 21}, {'m', 17},
    {'n', 29}, {'o', 22}, {'p', 13}, {'q', 34}, {'r', 25}, {'s', 8},
    {'t', 33}, {'u', 4}, {'v', 32}, {'w', 9}, {'x', 31}, {'y', 3}, {'z', 2}};

void tokenize(const std::string& text, std::vector<int64_t>& ids,
              std::vector<int64_t>& mask) {
  if (text.empty()) throw std::runtime_error("text must not be empty");
  ids.assign(kMaxLength, 0);
  mask.assign(kMaxLength, 0);
  size_t position = 0;
  for (const unsigned char raw : text) {
    if (raw >= 128)
      throw std::runtime_error("this English model accepts ASCII text only");
    const char character = static_cast<char>(std::tolower(raw));
    const auto token = kVocab.find(character);
    if (token == kVocab.end())
      throw std::runtime_error(std::string("unsupported character: ") +
                               character);
    if (position + 2 >= ids.size())
      throw std::runtime_error("text too long: maximum is 99 characters");
    ids[position++] = 0;
    ids[position++] = token->second;
  }
  ids[position++] = 0;
  std::fill(mask.begin(), mask.begin() + static_cast<long>(position), 1);
}

void run_encoder(Model& model, std::vector<int64_t>& ids,
                 std::vector<int64_t>& mask, std::vector<float>& log_duration,
                 std::vector<float>& input_mask, std::vector<float>& means,
                 std::vector<float>& log_vars) {
  rknn_input inputs[2]{};
  inputs[0].index = 0;
  inputs[0].type = RKNN_TENSOR_INT64;
  inputs[0].size = ids.size() * sizeof(int64_t);
  inputs[0].buf = ids.data();
  inputs[1].index = 1;
  inputs[1].type = RKNN_TENSOR_INT64;
  inputs[1].size = mask.size() * sizeof(int64_t);
  inputs[1].buf = mask.data();
  require_rknn(rknn_inputs_set(model.context(), 2, inputs),
               "encoder inputs_set");
  require_rknn(rknn_run(model.context(), nullptr), "encoder run");
  rknn_output outputs[4]{};
  for (auto& output : outputs) output.want_float = 1;
  require_rknn(rknn_outputs_get(model.context(), 4, outputs, nullptr),
               "encoder outputs_get");
  std::memcpy(log_duration.data(), outputs[0].buf,
              log_duration.size() * sizeof(float));
  std::memcpy(input_mask.data(), outputs[1].buf,
              input_mask.size() * sizeof(float));
  std::memcpy(means.data(), outputs[2].buf, means.size() * sizeof(float));
  std::memcpy(log_vars.data(), outputs[3].buf, log_vars.size() * sizeof(float));
  require_rknn(rknn_outputs_release(model.context(), 4, outputs),
               "encoder outputs_release");
}

int build_attention(const std::vector<float>& log_duration,
                    const std::vector<float>& input_mask,
                    float speaking_rate, std::vector<float>& attention,
                    std::vector<float>& output_mask) {
  std::vector<float> duration(kMaxLength), cumulative(kMaxLength);
  const float length_scale = 1.0f / speaking_rate;
  for (int i = 0; i < kMaxLength; ++i)
    duration[i] = std::ceil(std::exp(log_duration[i]) * input_mask[i] *
                            length_scale);
  std::partial_sum(duration.begin(), duration.end(), cumulative.begin());
  const int predicted = std::max(
      1, static_cast<int>(std::accumulate(duration.begin(), duration.end(), 0.0f)));
  if (predicted > kPredictedMax)
    throw std::runtime_error(
        "predicted audio exceeds fixed decoder length (400 frames)");

  output_mask.assign(kPredictedMax, 0);
  std::fill(output_mask.begin(), output_mask.begin() + predicted, 1.0f);
  attention.assign(static_cast<size_t>(kPredictedMax) * kMaxLength, 0);
  for (int token = 0; token < kMaxLength; ++token) {
    const int begin = token ? static_cast<int>(cumulative[token - 1]) : 0;
    const int end = std::min(predicted, static_cast<int>(cumulative[token]));
    for (int frame = std::max(0, begin); frame < end; ++frame)
      attention[static_cast<size_t>(frame) * kMaxLength + token] = 1.0f;
  }
  return predicted;
}

void run_decoder(Model& model, std::vector<float>& attention,
                 std::vector<float>& output_mask, std::vector<float>& means,
                 std::vector<float>& log_vars, std::vector<float>& audio) {
  rknn_input inputs[4]{};
  std::vector<float>* buffers[4] = {&attention, &output_mask, &means, &log_vars};
  for (int i = 0; i < 4; ++i) {
    inputs[i].index = i;
    inputs[i].type = RKNN_TENSOR_FLOAT32;
    inputs[i].size = buffers[i]->size() * sizeof(float);
    inputs[i].buf = buffers[i]->data();
  }
  inputs[0].fmt = RKNN_TENSOR_NHWC;
  require_rknn(rknn_inputs_set(model.context(), 4, inputs),
               "decoder inputs_set");
  require_rknn(rknn_run(model.context(), nullptr), "decoder run");
  rknn_output output{};
  output.want_float = 1;
  require_rknn(rknn_outputs_get(model.context(), 1, &output, nullptr),
               "decoder outputs_get");
  std::memcpy(audio.data(), output.buf, audio.size() * sizeof(float));
  require_rknn(rknn_outputs_release(model.context(), 1, &output),
               "decoder outputs_release");
}
}  // namespace

class TtsEngine::Impl {
 public:
  Impl(const std::string& encoder_path, const std::string& decoder_path)
      : encoder(encoder_path), decoder(decoder_path) {
    if (encoder.io().n_input != 2 || encoder.io().n_output != 4 ||
        decoder.io().n_input != 4 || decoder.io().n_output != 1)
      throw std::runtime_error("unexpected model I/O counts");
  }
  Model encoder;
  Model decoder;
};

TtsEngine::TtsEngine(const std::string& encoder_path,
                     const std::string& decoder_path)
    : impl_(std::make_unique<Impl>(encoder_path, decoder_path)) {}
TtsEngine::~TtsEngine() = default;

std::string TtsEngine::summary() const {
  return impl_->encoder.summary("encoder") + "\n" +
         impl_->decoder.summary("decoder");
}

SynthesisResult TtsEngine::synthesize(const std::string& text,
                                      float speaking_rate) {
  SynthesisResult result;
  const auto preprocess_begin = Clock::now();
  std::vector<int64_t> ids, mask;
  tokenize(text, ids, mask);
  const auto encoder_begin = Clock::now();

  std::vector<float> log_duration(kMaxLength), input_mask(kMaxLength);
  std::vector<float> means(kMaxLength * kPriorChannels);
  std::vector<float> log_vars(kMaxLength * kPriorChannels);
  run_encoder(impl_->encoder, ids, mask, log_duration, input_mask, means,
              log_vars);
  const auto middle_begin = Clock::now();

  std::vector<float> attention, output_mask;
  const int frames = build_attention(log_duration, input_mask, speaking_rate,
                                     attention, output_mask);
  const auto decoder_begin = Clock::now();
  result.audio.resize(static_cast<size_t>(frames) * kHopLength);
  run_decoder(impl_->decoder, attention, output_mask, means, log_vars,
              result.audio);
  const auto done = Clock::now();

  result.preprocess_ms = elapsed_ms(preprocess_begin, encoder_begin);
  result.encoder_ms = elapsed_ms(encoder_begin, middle_begin);
  result.middle_ms = elapsed_ms(middle_begin, decoder_begin);
  result.decoder_ms = elapsed_ms(decoder_begin, done);
  return result;
}
