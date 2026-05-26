#include "services/swarm/piece_scheduler.hpp"

#include <set>

std::map<std::string, std::vector<std::uint64_t>> PieceScheduler::plan_requests(
    const std::vector<std::uint64_t>& missing,
    const std::vector<PeerPieceAvailability>& peers,
    std::size_t max_in_flight_per_peer) const {
  std::map<std::string, std::vector<std::uint64_t>> plan;
  std::set<std::uint64_t> assigned;

  for (const auto& peer : peers) {
    auto& bucket = plan[peer.peer_key];
    for (const auto piece_index : missing) {
      if (bucket.size() >= max_in_flight_per_peer) {
        break;
      }
      if (piece_index >= peer.bitfield.size() || !peer.bitfield[piece_index] || assigned.count(piece_index) > 0) {
        continue;
      }
      bucket.push_back(piece_index);
      assigned.insert(piece_index);
    }
  }

  return plan;
}
