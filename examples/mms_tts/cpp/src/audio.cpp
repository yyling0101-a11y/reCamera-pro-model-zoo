#include "audio.h"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace {
void put16(std::ofstream& file, uint16_t value) {
  const char bytes[2] = {char(value), char(value >> 8)};
  file.write(bytes, 2);
}

void put32(std::ofstream& file, uint32_t value) {
  const char bytes[4] = {char(value), char(value >> 8), char(value >> 16),
                         char(value >> 24)};
  file.write(bytes, 4);
}
}  // namespace

void save_wav(const std::string& path, const std::vector<float>& audio) {
  std::ofstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("cannot create WAV: " + path);
  const uint32_t data_bytes = audio.size() * sizeof(int16_t);
  file.write("RIFF", 4);
  put32(file, 36 + data_bytes);
  file.write("WAVEfmt ", 8);
  put32(file, 16);
  put16(file, 1);
  put16(file, 1);
  put32(file, kAudioSampleRate);
  put32(file, kAudioSampleRate * sizeof(int16_t));
  put16(file, sizeof(int16_t));
  put16(file, 16);
  file.write("data", 4);
  put32(file, data_bytes);
  for (float sample : audio) {
    sample = std::clamp(sample, -1.0f, 1.0f);
    put16(file, static_cast<uint16_t>(
                    static_cast<int16_t>(std::lrint(sample * 32767))));
  }
  if (!file) throw std::runtime_error("failed writing WAV: " + path);
}

int play_wav(const std::string& path, const std::string& device) {
  const pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    if (device.empty())
      execlp("aplay", "aplay", "-q", path.c_str(), (char*)nullptr);
    else
      execlp("aplay", "aplay", "-q", "-D", device.c_str(), path.c_str(),
             (char*)nullptr);
    _exit(127);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
