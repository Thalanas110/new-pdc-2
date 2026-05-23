#include "views/http_view.hpp"

#include <algorithm>
#include <sstream>

namespace {

bool send_all(socket_t socket_handle, const char* data, std::size_t size) {
  std::size_t sent = 0;
  while (sent < size) {
    const auto chunk = static_cast<int>(std::min<std::size_t>(size - sent, 64 * 1024));
    const int result = send(socket_handle, data + sent, chunk, 0);
    if (result <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(result);
  }
  return true;
}

}  // namespace

void HttpView::send_response(socket_t client,
                             int status,
                             const std::string& status_text,
                             const std::string& content_type,
                             const std::string& body) const {
  std::ostringstream response;
  response << "HTTP/1.1 " << status << ' ' << status_text << "\r\n";
  response << "Content-Type: " << content_type << "\r\n";
  response << "Content-Length: " << body.size() << "\r\n";
  response << "Connection: close\r\n";
  response << "Access-Control-Allow-Origin: *\r\n";
  response << "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n";
  response << "Access-Control-Allow-Headers: Content-Type,X-File-Name,X-Peer-Host,X-Peer-Port\r\n\r\n";
  response << body;
  const std::string payload = response.str();
  send_all(client, payload.data(), payload.size());
}

void HttpView::send_json(socket_t client, int status, const std::string& status_text, const std::string& body) const {
  send_response(client, status, status_text, "application/json", body);
}

