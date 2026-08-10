#include "config.h"

#include <stdexcept>

Config parse_args(int argc, char** argv) {
  if (argc < 3) {
    throw std::runtime_error(
        std::string("usage: ") + argv[0] +
        " ENCODER.rknn DECODER.rknn [--port 8080] [--speaking-rate 0.8] "
        "[--output-dir /tmp] [--device NAME] [--no-play] [--keep-wav]");
  }

  Config config;
  config.encoder = argv[1];
  config.decoder = argv[2];
  for (int i = 3; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--port" && i + 1 < argc)
      config.port = std::stoi(argv[++i]);
    else if (arg == "--speaking-rate" && i + 1 < argc)
      config.speaking_rate = std::stof(argv[++i]);
    else if (arg == "--output-dir" && i + 1 < argc)
      config.output_dir = argv[++i];
    else if (arg == "--device" && i + 1 < argc)
      config.device = argv[++i];
    else if (arg == "--no-play")
      config.no_play = true;
    else if (arg == "--keep-wav")
      config.keep_wav = true;
    else
      throw std::runtime_error("unknown argument: " + arg);
  }

  if (config.port < 1 || config.port > 65535)
    throw std::runtime_error("invalid port");
  if (config.speaking_rate < 0.5f || config.speaking_rate > 1.5f)
    throw std::runtime_error("speaking rate must be between 0.5 and 1.5");
  return config;
}

