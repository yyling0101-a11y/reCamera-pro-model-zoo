#pragma once

#include <memory>
#include <string>
#include <vector>

class RknnZipformer {
public:
    RknnZipformer(const std::string &encoder_path, const std::string &decoder_path,
                  const std::string &joiner_path, const std::string &vocab_path);
    ~RknnZipformer();
    RknnZipformer(const RknnZipformer &) = delete;
    RknnZipformer &operator=(const RknnZipformer &) = delete;

    // Feed consecutive mono 16-kHz samples. Returns only tokens emitted by this chunk.
    std::string accept_waveform(const std::vector<float> &audio);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
