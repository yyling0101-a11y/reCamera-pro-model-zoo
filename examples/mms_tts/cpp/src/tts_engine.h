#pragma once

#include <memory>
#include <string>
#include <vector>

struct SynthesisResult {
  std::vector<float> audio;
  double preprocess_ms = 0;
  double encoder_ms = 0;
  double middle_ms = 0;
  double decoder_ms = 0;
};

class TtsEngine {
 public:
  TtsEngine(const std::string& encoder_path,
            const std::string& decoder_path);
  ~TtsEngine();
  TtsEngine(const TtsEngine&) = delete;
  TtsEngine& operator=(const TtsEngine&) = delete;

  SynthesisResult synthesize(const std::string& text, float speaking_rate);
  std::string summary() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

