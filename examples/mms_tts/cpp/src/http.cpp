#include "http.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace {
constexpr size_t kMaxRequestBytes = 1024 * 1024;

std::string extract_json_text(const std::string& body) {
  const auto key = body.find("\"text\"");
  if (key == std::string::npos)
    throw std::runtime_error("JSON requires a text field");
  const auto colon = body.find(':', key + 6);
  const auto quote = body.find('"', colon + 1);
  if (colon == std::string::npos || quote == std::string::npos)
    throw std::runtime_error("invalid JSON text field");

  std::string result;
  bool escaped = false;
  for (size_t i = quote + 1; i < body.size(); ++i) {
    const char c = body[i];
    if (escaped) {
      if (c == 'n') result += '\n';
      else if (c == 'r') result += '\r';
      else if (c == 't') result += '\t';
      else if (c == '"' || c == '\\' || c == '/') result += c;
      else throw std::runtime_error("unsupported JSON escape");
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      return result;
    } else {
      result += c;
    }
  }
  throw std::runtime_error("unterminated JSON string");
}
}  // namespace

int create_server(int port) {
  const int server = socket(AF_INET, SOCK_STREAM, 0);
  if (server < 0) throw std::runtime_error("socket failed");

  int yes = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(port);
  if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
    throw std::runtime_error("bind failed: " + std::string(strerror(errno)));
  if (listen(server, 8) < 0) throw std::runtime_error("listen failed");
  return server;
}

HttpRequest receive_request(int fd) {
  std::string data;
  char buffer[4096];
  size_t header_end = std::string::npos;
  size_t content_length = 0;
  while (data.size() < kMaxRequestBytes) {
    const ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
    if (count <= 0) break;
    data.append(buffer, count);
    header_end = data.find("\r\n\r\n");
    if (header_end == std::string::npos) continue;

    auto position = data.find("Content-Length:");
    if (position == std::string::npos)
      position = data.find("content-length:");
    if (position != std::string::npos)
      content_length = std::stoul(data.substr(position + 15));
    if (data.size() >= header_end + 4 + content_length) break;
  }
  if (header_end == std::string::npos)
    throw std::runtime_error("invalid HTTP request");

  HttpRequest request;
  const auto line_end = data.find("\r\n");
  std::istringstream first_line(data.substr(0, line_end));
  first_line >> request.method >> request.path;
  request.body = data.substr(header_end + 4, content_length);
  return request;
}

void send_json(int fd, int status, const std::string& body) {
  std::ostringstream response;
  response << "HTTP/1.1 " << status
           << (status == 200 ? " OK" : " Bad Request")
           << "\r\nContent-Type: application/json\r\nContent-Length: "
           << body.size() << "\r\nConnection: close\r\n\r\n" << body;
  const std::string bytes = response.str();
  size_t sent = 0;
  while (sent < bytes.size()) {
    const ssize_t count =
        send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
    if (count <= 0) break;
    sent += count;
  }
}

std::string request_text(std::string body) {
  const auto first = body.find_first_not_of(" \t\r\n");
  const auto last = body.find_last_not_of(" \t\r\n");
  if (first == std::string::npos)
    throw std::runtime_error("request body must not be empty");
  body = body.substr(first, last - first + 1);
  if (body.size() >= 2 && body.front() == '\'' && body.back() == '\'') {
    body = body.substr(1, body.size() - 2);
    const auto inner_first = body.find_first_not_of(" \t\r\n");
    if (inner_first != std::string::npos) body.erase(0, inner_first);
  }
  return body.front() == '{' ? extract_json_text(body) : body;
}

std::string json_escape(const std::string& text) {
  std::ostringstream escaped;
  for (const unsigned char c : text) {
    if (c == '"' || c == '\\') escaped << '\\' << c;
    else if (c == '\n') escaped << "\\n";
    else if (c < 32) escaped << '?';
    else escaped << c;
  }
  return escaped.str();
}

