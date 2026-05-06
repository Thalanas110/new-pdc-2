#pragma once

#include <cstdint>
#include <string>

namespace transfer {

std::string format_bytes(std::uint64_t bytes);
std::string content_type_for_file(const std::string& file_name);
std::string safe_file_name(const std::string& input);
bool is_loopback_host(const std::string& host);
bool is_private_lan_host(const std::string& host);
bool is_allowed_peer(const std::string& host, bool allow_remote_peers);
std::string json_escape(const std::string& input);
std::string make_transfer_id();

}  // namespace transfer
