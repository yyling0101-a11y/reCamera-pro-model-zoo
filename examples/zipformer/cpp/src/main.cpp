#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "rknn_zipformer.h"

namespace
{
constexpr int kSampleRate = 16000;
constexpr int kChannels = 6;
std::atomic<bool> g_stop{false};

void on_signal(int) { g_stop.store(true); }

struct Options
{
  std::string encoder = "model/encoder-epoch-99-avg-1-rv1126b.rknn";
  std::string decoder = "model/decoder-epoch-99-avg-1-rv1126b.rknn";
  std::string joiner = "model/joiner-epoch-99-avg-1-rv1126b.rknn";
  std::string vocab = "model/vocab.txt";
  std::string device = "hw:0,0";
  int channel = 0;
  int chunk_ms = 960;
};

void usage(const char *program)
{
  std::cout << "用法: " << program << " [选项]\n"
            << "      --encoder PATH     Zipformer encoder RKNN 模型\n"
            << "      --decoder PATH     Zipformer decoder RKNN 模型\n"
            << "      --joiner PATH      Zipformer joiner RKNN 模型\n"
            << "      --vocab PATH       中英双语词表，默认 model/vocab.txt\n"
            << "  -d, --device NAME      ALSA 设备，默认 hw:0,0\n"
            << "  -c, --channel N        使用通道 0..5；-1 表示平均，默认 0\n"
            << "      --chunk-ms N       每次送入流的音频长度，默认 960 ms\n"
            << "  -h, --help             显示帮助\n";
}

int parse_int(const char *value, const char *name)
{
  try
  {
    size_t used = 0;
    const int result = std::stoi(value, &used);
    if (used != std::strlen(value)) throw std::invalid_argument("trailing");
    return result;
  }
  catch (...)
  {
    throw std::runtime_error(std::string("无效的 ") + name + ": " + value);
  }
}

Options parse_args(int argc, char **argv)
{
  Options o;
  for (int i = 1; i < argc; ++i)
  {
    const std::string arg = argv[i];
    auto value = [&]() -> const char *
    {
      if (++i >= argc) throw std::runtime_error(arg + " 缺少参数");
      return argv[i];
    };
    if (arg == "--encoder")
      o.encoder = value();
    else if (arg == "--decoder")
      o.decoder = value();
    else if (arg == "--joiner")
      o.joiner = value();
    else if (arg == "--vocab")
      o.vocab = value();
    else if (arg == "-d" || arg == "--device")
      o.device = value();
    else if (arg == "-c" || arg == "--channel")
      o.channel = parse_int(value(), "channel");
    else if (arg == "--chunk-ms")
      o.chunk_ms = parse_int(value(), "chunk-ms");
    else if (arg == "-h" || arg == "--help")
    {
      usage(argv[0]);
      std::exit(0);
    }
    else
      throw std::runtime_error("未知参数: " + arg);
  }
  if (o.channel < -1 || o.channel >= kChannels)
    throw std::runtime_error("channel 必须为 -1..5");
  if (o.chunk_ms < 100 || o.chunk_ms > 5000)
    throw std::runtime_error("chunk-ms 必须为 100..5000");
  return o;
}

class AudioBuffer
{
  public:
  void append(const float *samples, size_t count)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < count; ++i) samples_.push_back(samples[i]);
    cv_.notify_one();
  }

  bool wait_for_chunk(size_t count, std::vector<float> &out)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(250),
                 [&] { return g_stop.load() || samples_.size() >= count; });
    if (samples_.size() < count) return false;
    out.assign(samples_.begin(),
               samples_.begin() + static_cast<std::ptrdiff_t>(count));
    samples_.erase(samples_.begin(),
                   samples_.begin() + static_cast<std::ptrdiff_t>(count));
    return true;
  }

  private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<float> samples_;
};

struct CaptureStats
{
  std::array<std::atomic<float>, kChannels> channel_rms{};
  std::atomic<uint64_t> frames{0};
};

struct RecorderProcess
{
  pid_t pid = -1;
  int fd = -1;
  ~RecorderProcess() { stop(); }

  void start(const std::string &device)
  {
    int pipe_fd[2];
    if (pipe(pipe_fd) != 0)
      throw std::runtime_error("pipe 失败: " +
                               std::string(std::strerror(errno)));
    pid = fork();
    if (pid < 0) throw std::runtime_error("fork 失败");
    if (pid == 0)
    {
      close(pipe_fd[0]);
      if (dup2(pipe_fd[1], STDOUT_FILENO) < 0) _exit(126);
      close(pipe_fd[1]);
      execlp("arecord", "arecord", "-q", "-D", device.c_str(), "-t", "raw",
             "-f", "S16_LE", "-r", "16000", "-c", "6", "-", nullptr);
      _exit(127);
    }
    close(pipe_fd[1]);
    fd = pipe_fd[0];
  }

  void stop()
  {
    if (fd >= 0)
    {
      close(fd);
      fd = -1;
    }
    if (pid > 0)
    {
      kill(pid, SIGTERM);
      waitpid(pid, nullptr, 0);
      pid = -1;
    }
  }
};

void capture_loop(AudioBuffer &buffer, const Options &o, CaptureStats &stats,
                  std::atomic<bool> &failed)
{
  try
  {
    RecorderProcess recorder;
    recorder.start(o.device);
    constexpr size_t frame_bytes = sizeof(int16_t) * kChannels;
    std::vector<uint8_t> read_buffer(1024 * frame_bytes);
    std::vector<uint8_t> pending;
    std::vector<float> mono(1024);
    std::array<double, kChannels> level_sums{};
    size_t level_frames = 0;
    while (!g_stop.load())
    {
      const ssize_t bytes =
          read(recorder.fd, read_buffer.data(), read_buffer.size());
      if (bytes < 0 && errno == EINTR) continue;
      if (bytes <= 0) throw std::runtime_error("arecord 已退出或无法读取设备");
      pending.insert(pending.end(), read_buffer.begin(),
                     read_buffer.begin() + bytes);
      const size_t frames = pending.size() / frame_bytes;
      mono.resize(frames);
      for (size_t frame = 0; frame < frames; ++frame)
      {
        float sum = 0.0f;
        const int16_t *input = reinterpret_cast<const int16_t *>(
            pending.data() + frame * frame_bytes);
        for (int ch = 0; ch < kChannels; ++ch)
        {
          const float value = input[ch] / 32768.0f;
          level_sums[ch] += value * value;
        }
        if (o.channel >= 0)
        {
          sum = input[static_cast<size_t>(o.channel)];
        }
        else
        {
          for (int ch = 0; ch < kChannels; ++ch) sum += input[ch];
          sum /= kChannels;
        }
        mono[frame] = sum / 32768.0f;
      }
      buffer.append(mono.data(), frames);
      stats.frames.fetch_add(frames, std::memory_order_relaxed);
      level_frames += frames;
      if (level_frames >= static_cast<size_t>(kSampleRate))
      {
        for (int ch = 0; ch < kChannels; ++ch)
        {
          stats.channel_rms[ch].store(
              static_cast<float>(std::sqrt(level_sums[ch] / level_frames)),
              std::memory_order_relaxed);
          level_sums[ch] = 0.0;
        }
        level_frames = 0;
      }
      pending.erase(
          pending.begin(),
          pending.begin() + static_cast<std::ptrdiff_t>(frames * frame_bytes));
    }
  }
  catch (const std::exception &e)
  {
    std::cerr << "\n录音错误: " << e.what() << "\n";
    failed.store(true);
    g_stop.store(true);
  }
}

float rms(const std::vector<float> &audio)
{
  if (audio.empty()) return 0.0f;
  double sum = 0.0;
  for (float value : audio) sum += value * value;
  return static_cast<float>(std::sqrt(sum / audio.size()));
}

}  // namespace

int main(int argc, char **argv)
{
  try
  {
    const Options options = parse_args(argc, argv);
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    RknnZipformer recognizer(options.encoder, options.decoder, options.joiner,
                             options.vocab);

    const size_t chunk_samples =
        static_cast<size_t>(options.chunk_ms) * kSampleRate / 1000;
    AudioBuffer buffer;
    CaptureStats stats;
    std::atomic<bool> capture_failed{false};
    std::thread capture(capture_loop, std::ref(buffer), std::cref(options),
                        std::ref(stats), std::ref(capture_failed));

    std::cerr << "正在监听 " << options.device
              << " (S16_LE, 16000 Hz, 6 ch, 使用通道 " << options.channel
              << ")，按 Ctrl-C 停止...\n";
    while (!g_stop.load())
    {
      std::vector<float> audio;
      if (!buffer.wait_for_chunk(chunk_samples, audio)) continue;
      const float audio_rms = rms(audio);
      std::cerr << "[audio] frames=" << stats.frames.load()
                << " rms=" << audio_rms << " channels=";
      for (int ch = 0; ch < kChannels; ++ch)
      {
        if (ch) std::cerr << ',';
        std::cerr << stats.channel_rms[ch].load();
      }
      std::cerr << '\n';
      const std::string text = recognizer.accept_waveform(audio);
      if (!text.empty()) std::cout << text << std::flush;
    }
    g_stop.store(true);
    capture.join();
    return capture_failed.load() ? 2 : 0;
  }
  catch (const std::exception &e)
  {
    std::cerr << "错误: " << e.what() << "\n";
    usage(argv[0]);
    return 1;
  }
}
