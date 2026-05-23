#include "services/transfer/transfer_service.hpp"

#include "core/transfer_core.hpp"
#include "services/net-io/net_io.hpp"
#include "views/http_view.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <thread>

TransferService::TransferService(AppState& state, SharedSyncService& shared_sync_service)
    : state_(state), shared_sync_service_(shared_sync_service) {}

std::optional<int> TransferService::parse_int(const std::string& value) {
  int parsed = 0;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  return parsed;
}

void TransferService::send_file_to_peer(socket_t client,
                                        const std::string& host,
                                        int port,
                                        const std::string& file_name,
                                        const std::vector<char>& body) {
  static const HttpView view;

  if (!transfer::is_allowed_peer(host, state_.allow_remote_peers)) {
    view.send_json(client,
                   400,
                   "Bad Request",
                   "{\"ok\":false,\"error\":\"Only localhost and private LAN peers are allowed\"}");
    return;
  }
  state_.add_sync_peer(transfer::PeerEndpoint{host, port});

  TransferRecord record;
  record.direction = "outgoing";
  record.file_name = file_name;
  record.status = "transferring";
  record.peer = host + ":" + std::to_string(port);
  record.size = static_cast<std::uint64_t>(body.size());
  record.message = "Opening peer socket";
  const std::string id = state_.add_transfer(record);

  std::filesystem::create_directories(state_.sent_dir);
  const std::filesystem::path sent_path = FileVaultService::unique_received_path(state_.sent_dir, file_name);
  std::ofstream sent_copy(sent_path, std::ios::binary);
  if (sent_copy) {
    sent_copy.write(body.data(), static_cast<std::streamsize>(body.size()));
  }

  const socket_t peer = netio::connect_to_peer(host, port);
  if (peer == invalid_socket) {
    state_.update_transfer(id, 0, "failed", "Could not connect to peer");
    view.send_json(client, 502, "Bad Gateway", "{\"ok\":false,\"error\":\"Could not connect to peer\"}");
    return;
  }

  const std::string header =
      "LOOPLINE/1\n" + file_name + "\n" + std::to_string(body.size()) + "\n\n";
  if (!netio::send_text(peer, header)) {
    state_.update_transfer(id, 0, "failed", "Could not send transfer header");
    close_socket(peer);
    view.send_json(client, 502, "Bad Gateway", "{\"ok\":false,\"error\":\"Could not send header\"}");
    return;
  }

  std::uint64_t sent = 0;
  while (sent < body.size()) {
    const auto chunk = static_cast<int>(std::min<std::size_t>(64 * 1024, body.size() - static_cast<std::size_t>(sent)));
    const int result = send(peer, body.data() + sent, chunk, 0);
    if (result <= 0) {
      state_.update_transfer(id, sent, "failed", "Peer closed the transfer");
      close_socket(peer);
      view.send_json(client, 502, "Bad Gateway", "{\"ok\":false,\"error\":\"Peer closed the transfer\"}");
      return;
    }
    sent += static_cast<std::uint64_t>(result);
    state_.update_transfer(id, sent, "transferring", "Sent " + transfer::format_bytes(sent));
  }

  close_socket(peer);
  state_.update_transfer(id, sent, "complete", "Delivered to " + host + ":" + std::to_string(port));
  view.send_json(client, 200, "OK", "{\"ok\":true,\"id\":\"" + transfer::json_escape(id) + "\"}");
}

void TransferService::handle_incoming_transfer(socket_t client, const std::string& peer) {
  std::string magic;
  if (!netio::read_line(client, magic)) {
    close_socket(client);
    return;
  }

  if (shared_sync_service_.handle_shared_protocol(client, magic, peer)) {
    return;
  }

  std::string file_name;
  std::string size_line;
  std::string blank;
  if (!netio::read_line(client, file_name) || !netio::read_line(client, size_line) || !netio::read_line(client, blank) ||
      magic != "LOOPLINE/1") {
    close_socket(client);
    return;
  }

  const auto parsed_size = parse_int(size_line);
  if (!parsed_size || *parsed_size < 0) {
    close_socket(client);
    return;
  }

  const std::string safe_name = transfer::safe_file_name(file_name);
  const std::uint64_t total_size = static_cast<std::uint64_t>(*parsed_size);
  TransferRecord record;
  record.direction = "incoming";
  record.file_name = safe_name;
  record.status = "transferring";
  record.peer = peer;
  record.size = total_size;
  record.message = "Receiving " + transfer::format_bytes(total_size);
  const std::string id = state_.add_transfer(record);

  std::filesystem::create_directories(state_.receive_dir);
  const std::filesystem::path destination = FileVaultService::unique_received_path(state_.receive_dir, safe_name);
  std::ofstream output(destination, std::ios::binary);
  if (!output) {
    state_.update_transfer(id, 0, "failed", "Could not open receive file");
    close_socket(client);
    return;
  }

  std::uint64_t received_total = 0;
  std::vector<char> buffer(64 * 1024);
  while (received_total < total_size) {
    const auto remaining = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), total_size - received_total));
    const int received = recv(client, buffer.data(), static_cast<int>(remaining), 0);
    if (received <= 0) {
      state_.update_transfer(id, received_total, "failed", "Connection closed early");
      close_socket(client);
      return;
    }
    output.write(buffer.data(), received);
    received_total += static_cast<std::uint64_t>(received);
    state_.update_transfer(id,
                           received_total,
                           "transferring",
                           "Received " + transfer::format_bytes(received_total));
  }

  state_.update_transfer(id, received_total, "complete", "Saved to " + destination.string());
  close_socket(client);
}

void TransferService::transfer_listener() {
  const socket_t listener = netio::create_listener(state_.bind_host, state_.transfer_port);
  if (listener == invalid_socket) {
    std::cerr << "Could not start transfer listener on " << state_.bind_host << ':' << state_.transfer_port << '\n';
    return;
  }

  state_.listener_active = true;
  while (state_.running.load()) {
    sockaddr_in peer_address{};
    socket_length_t peer_size = sizeof(peer_address);
    const socket_t client = accept(listener, reinterpret_cast<sockaddr*>(&peer_address), &peer_size);
    if (client == invalid_socket) {
      continue;
    }

    char peer_buffer[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &peer_address.sin_addr, peer_buffer, sizeof(peer_buffer));
    std::thread(&TransferService::handle_incoming_transfer, this, client, std::string(peer_buffer)).detach();
  }

  close_socket(listener);
}

