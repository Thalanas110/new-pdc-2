#include "services/file-vault/file_vault_service.hpp"

#include "core/transfer_core.hpp"
#include "services/net-io/net_io.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

FileVaultService::FileVaultService(AppState& state) : state_(state) {}

std::string FileVaultService::url_encode(const std::string& input) {
  std::ostringstream output;
  output << std::hex << std::uppercase;
  for (const unsigned char ch : input) {
    const bool safe = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                      (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
                      ch == '.' || ch == '~';
    if (safe) {
      output << static_cast<char>(ch);
    } else {
      output << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
  }
  return output.str();
}

std::filesystem::path FileVaultService::unique_received_path(const std::filesystem::path& directory,
                                                             const std::string& file_name) {
  std::filesystem::path candidate = directory / file_name;
  if (!std::filesystem::exists(candidate)) {
    return candidate;
  }

  const std::filesystem::path stem = candidate.stem();
  const std::filesystem::path extension = candidate.extension();
  for (int index = 1; index < 1000; ++index) {
    std::ostringstream name;
    name << stem.string() << " (" << index << ")" << extension.string();
    candidate = directory / name.str();
    if (!std::filesystem::exists(candidate)) {
      return candidate;
    }
  }

  return directory / (transfer::make_transfer_id() + "-" + file_name);
}

std::optional<std::string> FileVaultService::file_list_json(const std::string& kind) const {
  const auto directory = state_.directory_for_kind(kind);
  if (!directory) {
    return std::nullopt;
  }

  std::error_code error;
  std::filesystem::create_directories(*directory, error);
  std::filesystem::directory_iterator iterator(*directory, error);
  const std::filesystem::directory_iterator end;

  std::ostringstream out;
  out << "{\"ok\":true,\"kind\":\"" << transfer::json_escape(kind) << "\",\"files\":[";

  bool first = true;
  while (!error && iterator != end) {
    const auto& entry = *iterator;
    if (entry.is_regular_file(error) && !error) {
      if (!first) {
        out << ',';
      }
      first = false;

      const std::string name = entry.path().filename().string();
      const auto size = entry.file_size(error);
      const auto modified = entry.last_write_time(error);
      const auto modified_tick = error ? 0ll : static_cast<long long>(modified.time_since_epoch().count());
      const std::string content_type = transfer::content_type_for_file(name);
      const std::string encoded_name = url_encode(name);

      out << "{\"kind\":\"" << transfer::json_escape(kind) << "\",";
      out << "\"name\":\"" << transfer::json_escape(name) << "\",";
      out << "\"size\":" << (error ? 0 : static_cast<unsigned long long>(size)) << ',';
      out << "\"modifiedAt\":\"" << modified_tick << "\",";
      out << "\"contentType\":\"" << transfer::json_escape(content_type) << "\",";
      out << "\"url\":\"/api/files/open?kind=" << transfer::json_escape(kind)
          << "&name=" << transfer::json_escape(encoded_name) << "\",";
      out << "\"downloadUrl\":\"/api/files/download?kind=" << transfer::json_escape(kind)
          << "&name=" << transfer::json_escape(encoded_name) << "\"}";
    }
    iterator.increment(error);
  }

  out << "]}";
  return out.str();
}

void FileVaultService::send_file_with_disposition(socket_t client,
                                                   const std::string& kind,
                                                   const std::string& raw_name,
                                                   const std::string& disposition) const {
  const auto directory = state_.directory_for_kind(kind);
  if (!directory) {
    view_.send_json(client, 400, "Bad Request", "{\"ok\":false,\"error\":\"Invalid file kind\"}");
    return;
  }

  const std::string file_name = transfer::safe_file_name(raw_name);
  const std::filesystem::path file_path = *directory / file_name;
  if (!std::filesystem::exists(file_path) || !std::filesystem::is_regular_file(file_path)) {
    view_.send_json(client, 404, "Not Found", "{\"ok\":false,\"error\":\"File not found\"}");
    return;
  }

  std::ifstream input(file_path, std::ios::binary);
  if (!input) {
    view_.send_json(client, 500, "Internal Server Error", "{\"ok\":false,\"error\":\"Could not open file\"}");
    return;
  }

  const auto size = std::filesystem::file_size(file_path);
  std::ostringstream headers;
  headers << "HTTP/1.1 200 OK\r\n";
  headers << "Content-Type: " << transfer::content_type_for_file(file_name) << "\r\n";
  headers << "Content-Length: " << size << "\r\n";
  headers << "Content-Disposition: " << disposition << "; filename=\"" << transfer::json_escape(file_name) << "\"\r\n";
  headers << "Connection: close\r\n";
  headers << "Access-Control-Allow-Origin: *\r\n\r\n";
  if (!netio::send_text(client, headers.str())) {
    return;
  }

  std::vector<char> buffer(64 * 1024);
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read = input.gcount();
    if (read > 0 && !netio::send_all(client, buffer.data(), static_cast<std::size_t>(read))) {
      return;
    }
  }
}

void FileVaultService::send_file_inline(socket_t client,
                                        const std::string& kind,
                                        const std::string& raw_name) const {
  send_file_with_disposition(client, kind, raw_name, "inline");
}

void FileVaultService::send_file_attachment(socket_t client,
                                            const std::string& kind,
                                            const std::string& raw_name) const {
  send_file_with_disposition(client, kind, raw_name, "attachment");
}

void FileVaultService::save_shared_upload(socket_t client,
                                          const std::string& file_name,
                                          const std::vector<char>& body) {
  std::filesystem::create_directories(state_.shared_dir);
  const std::filesystem::path destination = unique_received_path(state_.shared_dir, file_name);
  std::ofstream output(destination, std::ios::binary);
  if (!output) {
    view_.send_json(client, 500, "Internal Server Error", "{\"ok\":false,\"error\":\"Could not open shared file\"}");
    return;
  }

  output.write(body.data(), static_cast<std::streamsize>(body.size()));
  if (!output) {
    view_.send_json(client, 500, "Internal Server Error", "{\"ok\":false,\"error\":\"Could not save shared file\"}");
    return;
  }

  TransferRecord record;
  record.direction = "outgoing";
  record.file_name = destination.filename().string();
  record.status = "complete";
  record.peer = "shared";
  record.size = static_cast<std::uint64_t>(body.size());
  record.bytes_transferred = static_cast<std::uint64_t>(body.size());
  record.message = "Added to shared folder";
  state_.add_transfer(record);

  view_.send_json(client,
                  200,
                  "OK",
                  "{\"ok\":true,\"name\":\"" + transfer::json_escape(destination.filename().string()) + "\"}");
}

