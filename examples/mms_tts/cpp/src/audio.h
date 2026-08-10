#pragma once

#include <string>
#include <vector>

constexpr int kAudioSampleRate = 16000;

void save_wav(const std::string& path, const std::vector<float>& audio);
int play_wav(const std::string& path, const std::string& device);

