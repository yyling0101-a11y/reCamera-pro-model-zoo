#pragma once

#include <string>

struct HttpRequest {
  std::string method;
  std::string path;
  std::string body;
};

int create_server(int port);
HttpRequest receive_request(int fd);
void send_json(int fd, int status, const std::string& body);
std::string request_text(std::string body);
std::string json_escape(const std::string& text);

