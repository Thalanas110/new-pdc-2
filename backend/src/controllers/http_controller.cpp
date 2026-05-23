#include "controllers/http_controller.hpp"

#include <algorithm>
#include <charconv>
#include <sstream>

HttpController::HttpController(HttpView view, Dependencies deps) : view_(std::move(view)), deps_(std::move(deps)) {}

void HttpController::handle_client(socket_t client) const {
  HttpRequest request;
  if (!read_http_request(client, request)) {
    view_.send_json(client, 400, "Bad Request", "{\"ok\":false,\"error\":\"Malformed request\"}");
    close_socket(client);
    return;
  }

  const std::string route = path_only(request.target);
  if (request.method == "OPTIONS") {
    view_.send_response(client, 204, "No Content", "text/plain", "");
  } else if (request.method == "GET" && route == "/api/health") {
    view_.send_json(client, 200, "OK", "{\"ok\":true}");
  } else if (request.method == "GET" && route == "/api/status") {
    view_.send_json(client, 200, "OK", deps_.status_json());
  } else if (request.method == "GET" && route == "/api/files") {
    const std::string kind = query_value(request.target, "kind").empty() ? "received" : query_value(request.target, "kind");
    const auto listing = deps_.file_list_json(kind);
    if (!listing.has_value()) {
      view_.send_json(client, 400, "Bad Request", "{\"ok\":false,\"error\":\"Invalid file kind\"}");
    } else {
      view_.send_json(client, 200, "OK", *listing);
    }
  } else if (request.method == "GET" && route == "/api/files/open") {
    const std::string kind = query_value(request.target, "kind").empty() ? "received" : query_value(request.target, "kind");
    const std::string name = query_value(request.target, "name");
    deps_.send_file_inline(client, kind, name);
  } else if (request.method == "POST" && route == "/api/sync/peers") {
    const std::string host = query_value(request.target, "host");
    const int port = parse_int(query_value(request.target, "port")).value_or(deps_.transfer_port());
    const auto peer = transfer::parse_peer_endpoint(host + ":" + std::to_string(port), deps_.transfer_port());
    if (!peer || !deps_.is_allowed_peer(peer->host)) {
      view_.send_json(client,
                      400,
                      "Bad Request",
                      "{\"ok\":false,\"error\":\"Use localhost or a private LAN peer that this backend allows\"}");
    } else {
      deps_.add_sync_peer(*peer);
      view_.send_json(client, 200, "OK", deps_.status_json());
    }
  } else if (request.method == "POST" && route == "/api/sync/peers/remove") {
    const std::string host = query_value(request.target, "host");
    const int port = parse_int(query_value(request.target, "port")).value_or(deps_.transfer_port());
    const auto peer = transfer::parse_peer_endpoint(host + ":" + std::to_string(port), deps_.transfer_port());
    if (!peer) {
      view_.send_json(client, 400, "Bad Request", "{\"ok\":false,\"error\":\"Invalid sync peer\"}");
    } else {
      deps_.remove_sync_peer(*peer);
      view_.send_json(client, 200, "OK", deps_.status_json());
    }
  } else if (request.method == "POST" && route == "/api/receive/start") {
    const std::string port_value = query_value(request.target, "port");
    const int requested_port = port_value.empty() ? deps_.transfer_port() : parse_int(port_value).value_or(deps_.transfer_port());
    if (requested_port != deps_.transfer_port()) {
      view_.send_json(client,
                      409,
                      "Conflict",
                      "{\"ok\":false,\"error\":\"Restart the backend with the requested transfer port\"}");
    } else if (deps_.listener_active()) {
      view_.send_json(client, 200, "OK", "{\"ok\":true,\"message\":\"Receiver already active\"}");
    } else {
      view_.send_json(client, 503, "Service Unavailable", "{\"ok\":false,\"error\":\"Receiver is not active\"}");
    }
  } else if (request.method == "POST" && route == "/api/send") {
    const std::string raw_file_name = url_decode(header_value(request, "x-file-name", "download.bin"));
    const std::string file_name = transfer::safe_file_name(raw_file_name);
    const std::string peer_host = header_value(request, "x-peer-host", "127.0.0.1");
    const int peer_port = parse_int(header_value(request, "x-peer-port", std::to_string(deps_.transfer_port())))
                              .value_or(deps_.transfer_port());
    deps_.send_file_to_peer(client, peer_host, peer_port, file_name, request.body);
  } else if (request.method == "POST" && route == "/api/shared/sync") {
    view_.send_json(client, 200, "OK", deps_.sync_shared_folder_json());
  } else if (request.method == "POST" && route == "/api/shared/upload") {
    const std::string raw_file_name = url_decode(header_value(request, "x-file-name", "shared-upload.bin"));
    const std::string file_name = transfer::safe_file_name(raw_file_name);
    deps_.save_shared_upload(client, file_name, request.body);
  } else {
    view_.send_json(client, 404, "Not Found", "{\"ok\":false,\"error\":\"Not found\"}");
  }

  close_socket(client);
}

bool HttpController::read_http_request(socket_t client, HttpRequest& request) const {
  std::string raw;
  std::vector<char> buffer(4096);
  while (raw.find("\r\n\r\n") == std::string::npos) {
    const int received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
    if (received <= 0) {
      return false;
    }
    raw.append(buffer.data(), static_cast<std::size_t>(received));
    if (raw.size() > 64 * 1024) {
      return false;
    }
  }

  const auto header_end = raw.find("\r\n\r\n");
  const std::string headers = raw.substr(0, header_end);
  std::istringstream stream(headers);
  std::string request_line;
  std::getline(stream, request_line);
  if (!request_line.empty() && request_line.back() == '\r') {
    request_line.pop_back();
  }

  std::istringstream request_line_stream(request_line);
  request_line_stream >> request.method >> request.target;
  if (request.method.empty() || request.target.empty()) {
    return false;
  }

  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    std::string name = lower_copy(line.substr(0, colon));
    std::string value = line.substr(colon + 1);
    while (!value.empty() && value.front() == ' ') {
      value.erase(value.begin());
    }
    request.headers[name] = value;
  }

  const std::string content_length_header = header_value(request, "content-length", "0");
  const auto content_length = parse_int(content_length_header).value_or(0);
  if (content_length < 0) {
    return false;
  }

  const std::string already_read = raw.substr(header_end + 4);
  request.body.assign(already_read.begin(), already_read.end());
  while (request.body.size() < static_cast<std::size_t>(content_length)) {
    const std::size_t remaining = static_cast<std::size_t>(content_length) - request.body.size();
    const int received = recv(client,
                              buffer.data(),
                              static_cast<int>(std::min<std::size_t>(buffer.size(), remaining)),
                              0);
    if (received <= 0) {
      return false;
    }
    request.body.insert(request.body.end(), buffer.begin(), buffer.begin() + received);
  }

  return true;
}

std::string HttpController::lower_copy(std::string value) {
  for (char& ch : value) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return value;
}

std::optional<int> HttpController::parse_int(const std::string& value) {
  int parsed = 0;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  return parsed;
}

std::string HttpController::header_value(const HttpRequest& request,
                                         const std::string& name,
                                         const std::string& fallback) {
  const auto found = request.headers.find(lower_copy(name));
  if (found == request.headers.end()) {
    return fallback;
  }
  return found->second;
}

std::string HttpController::url_decode(const std::string& input) {
  std::string output;
  output.reserve(input.size());
  for (std::size_t index = 0; index < input.size(); ++index) {
    if (input[index] == '%' && index + 2 < input.size()) {
      const std::string hex = input.substr(index + 1, 2);
      int value = 0;
      std::istringstream(hex) >> std::hex >> value;
      output.push_back(static_cast<char>(value));
      index += 2;
    } else if (input[index] == '+') {
      output.push_back(' ');
    } else {
      output.push_back(input[index]);
    }
  }
  return output;
}

std::string HttpController::query_value(const std::string& target, const std::string& key) {
  const auto question = target.find('?');
  if (question == std::string::npos) {
    return "";
  }

  const std::string query = target.substr(question + 1);
  std::size_t start = 0;
  while (start <= query.size()) {
    const auto amp = query.find('&', start);
    const std::string pair = query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
    const auto equals = pair.find('=');
    const std::string name = url_decode(pair.substr(0, equals));
    const std::string value = equals == std::string::npos ? "" : url_decode(pair.substr(equals + 1));
    if (name == key) {
      return value;
    }
    if (amp == std::string::npos) {
      break;
    }
    start = amp + 1;
  }

  return "";
}

std::string HttpController::path_only(const std::string& target) {
  const auto question = target.find('?');
  return question == std::string::npos ? target : target.substr(0, question);
}

