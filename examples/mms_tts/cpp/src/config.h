#pragma once

#include <string>

struct Config {
  std::string encoder;
  std::string decoder;
  std::string output_dir = "/tmp";
  std::string device;
  int port = 8080;
  float speaking_rate = 0.8f;
  bool no_play = false;
  bool keep_wav = false;
};

Config parse_args(int argc, char** argv);

