#include "transfer_core.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

namespace transfer {
namespace {

std::string trim_copy(std::string value) {
  auto not_space = [](unsigned char ch) { return ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n'; };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

}  // namespace

std::string format_bytes(std::uint64_t bytes) {
  if (bytes < 1024) {
    return std::to_string(bytes) + " B";
  }

  const char* units[] = {"KB", "MB", "GB", "TB"};
  double value = static_cast<double>(bytes) / 1024.0;
  int unit_index = 0;

  while (value >= 1024.0 && unit_index < 3) {
    value /= 1024.0;
    ++unit_index;
  }

  std::ostringstream out;
  if (value == static_cast<std::uint64_t>(value)) {
    out << static_cast<std::uint64_t>(value);
  } else {
    out << std::fixed << std::setprecision(1) << value;
  }
  out << ' ' << units[unit_index];
  return out.str();
}

std::string safe_file_name(const std::string& input) {
  std::string name = trim_copy(input);
  const auto slash = name.find_last_of("/\\");
  if (slash != std::string::npos) {
    name = name.substr(slash + 1);
  }

  std::string cleaned;
  cleaned.reserve(name.size());
  for (char ch : name) {
    const bool allowed = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                         (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' ||
                         ch == '_' || ch == ' ';
    cleaned.push_back(allowed ? ch : '_');
  }

  cleaned = trim_copy(cleaned);
  if (cleaned.empty() || cleaned == "." || cleaned == "..") {
    return "download.bin";
  }
  return cleaned;
}

bool is_loopback_host(const std::string& host) {
  const std::string value = trim_copy(host);
  return value == "localhost" || value == "127.0.0.1" || value == "::1";
}

bool is_private_lan_host(const std::string& host) {
  const std::string value = trim_copy(host);
  std::vector<int> octets;
  std::size_t start = 0;
  while (start <= value.size()) {
    const auto dot = value.find('.', start);
    const std::string part = value.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    if (part.empty() || part.size() > 3) {
      return false;
    }

    int octet = 0;
    for (const char ch : part) {
      if (ch < '0' || ch > '9') {
        return false;
      }
      octet = (octet * 10) + (ch - '0');
    }
    if (octet < 0 || octet > 255 || std::to_string(octet) != part) {
      return false;
    }

    octets.push_back(octet);
    if (dot == std::string::npos) {
      break;
    }
    start = dot + 1;
  }

  if (octets.size() != 4) {
    return false;
  }

  return octets[0] == 10 || (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) ||
         (octets[0] == 192 && octets[1] == 168);
}

bool is_allowed_peer(const std::string& host, bool allow_remote_peers) {
  if (is_loopback_host(host)) {
    return true;
  }
  return allow_remote_peers && is_private_lan_host(host);
}

std::string json_escape(const std::string& input) {
  std::ostringstream out;
  for (const char ch : input) {
    switch (ch) {
      case '\\':
        out << "\\\\";
        break;
      case '"':
        out << "\\\"";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        out << ch;
        break;
    }
  }
  return out.str();
}

std::string make_transfer_id() {
  static std::mt19937_64 rng{std::random_device{}()};
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  std::ostringstream out;
  out << "tx-" << now << '-' << std::hex << rng();
  return out.str();
}

}  // namespace transfer
