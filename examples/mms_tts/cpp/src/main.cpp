#include "audio.h"
#include "config.h"
#include "http.h"
#include "tts_engine.h"

#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using Clock = std::chrono::steady_clock;
volatile std::sig_atomic_t g_stop = 0;

double elapsed_ms(Clock::time_point begin, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

void on_signal(int) { g_stop = 1; }

std::string benchmark_json(uint64_t request_id, const std::string& text,
                           const Config& config,
                           const SynthesisResult& synthesis,
                           double wav_write_ms, double playback_ms,
                           int playback_exit_code, double total_ms,
                           const std::string& wav_path) {
  const double audio_seconds =
      static_cast<double>(synthesis.audio.size()) / kAudioSampleRate;
  const double synthesis_ms = synthesis.preprocess_ms + synthesis.encoder_ms +
                              synthesis.middle_ms + synthesis.decoder_ms +
                              wav_write_ms;
  const double rtf = (synthesis_ms / 1000.0) / audio_seconds;

  std::ostringstream json;
  json << std::fixed << std::setprecision(3)
       << "{\"request_id\":" << request_id << ",\"text\":\""
       << json_escape(text) << "\",\"speaking_rate\":"
       << config.speaking_rate << ",\"audio_seconds\":" << audio_seconds
       << ",\"samples\":" << synthesis.audio.size()
       << ",\"preprocess_ms\":" << synthesis.preprocess_ms
       << ",\"encoder_ms\":" << synthesis.encoder_ms
       << ",\"middle_ms\":" << synthesis.middle_ms
       << ",\"decoder_ms\":" << synthesis.decoder_ms
       << ",\"wav_write_ms\":" << wav_write_ms
       << ",\"synthesis_ms\":" << synthesis_ms << ",\"rtf\":" << rtf
       << ",\"x_realtime\":" << 1.0 / rtf
       << ",\"playback_ms\":" << playback_ms
       << ",\"playback_exit_code\":" << playback_exit_code
       << ",\"total_ms\":" << total_ms << ",\"wav_path\":\""
       << (config.keep_wav ? json_escape(wav_path) : "") << "\"}";
  return json.str();
}

void handle_tts(int fd, uint64_t request_id, const HttpRequest& request,
                const Config& config, TtsEngine& engine,
                Clock::time_point request_begin) {
  const std::string text = request_text(request.body);
  SynthesisResult synthesis = engine.synthesize(text, config.speaking_rate);

  const auto wav_begin = Clock::now();
  const std::string wav_path = config.output_dir + "/tts_" +
                               std::to_string(getpid()) + "_" +
                               std::to_string(request_id) + ".wav";
  save_wav(wav_path, synthesis.audio);
  const auto playback_begin = Clock::now();
  const int playback_exit_code =
      config.no_play ? 0 : play_wav(wav_path, config.device);
  const auto done = Clock::now();

  const std::string json = benchmark_json(
      request_id, text, config, synthesis, elapsed_ms(wav_begin, playback_begin),
      elapsed_ms(playback_begin, done), playback_exit_code,
      elapsed_ms(request_begin, done), wav_path);
  std::cout << "BENCHMARK " << json << std::endl;
  send_json(fd, 200, json);
  if (!config.keep_wav) std::filesystem::remove(wav_path);
}

void serve(int server, const Config& config, TtsEngine& engine) {
  uint64_t request_id = 0;
  while (!g_stop) {
    const int fd = accept(server, nullptr, nullptr);
    if (fd < 0) {
      if (errno == EINTR) continue;
      throw std::runtime_error("accept failed");
    }

    ++request_id;
    const auto request_begin = Clock::now();
    try {
      const HttpRequest request = receive_request(fd);
      if (request.method == "GET" && request.path == "/health") {
        send_json(fd, 200, "{\"status\":\"ok\"}");
      } else if (request.method == "POST" && request.path == "/tts") {
        handle_tts(fd, request_id, request, config, engine, request_begin);
      } else {
        throw std::runtime_error("use POST /tts");
      }
    } catch (const std::exception& error) {
      const std::string body =
          "{\"error\":\"" + json_escape(error.what()) + "\"}";
      std::cerr << "request_id=" << request_id << " error=" << error.what()
                << std::endl;
      send_json(fd, 400, body);
    }
    close(fd);
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const Config config = parse_args(argc, argv);
    std::filesystem::create_directories(config.output_dir);

    const auto init_begin = Clock::now();
    TtsEngine engine(config.encoder, config.decoder);
    const auto init_done = Clock::now();
    std::cout << engine.summary()
              << "\nmodel_init_ms=" << elapsed_ms(init_begin, init_done)
              << std::endl;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);
    const int server = create_server(config.port);
    std::cout << "ready http://0.0.0.0:" << config.port
              << " POST /tts  speaking_rate=" << config.speaking_rate
              << " (models remain resident, requests are serialized)"
              << std::endl;
    serve(server, config, engine);
    close(server);
    std::cout << "stopped" << std::endl;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "fatal: " << error.what() << std::endl;
    return 1;
  }
}
