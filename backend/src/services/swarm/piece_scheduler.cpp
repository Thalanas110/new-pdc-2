#include "services/swarm/piece_scheduler.hpp"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace {

std::size_t availability_count(std::uint64_t piece_index, const std::vector<PeerPieceAvailability>& peers) {
  std::size_t count = 0;
  for (const auto& peer : peers) {
    if (piece_index < peer.bitfield.size() && peer.bitfield[piece_index]) {
      ++count;
    }
  }
  return count;
}

}  // namespace

std::map<std::string, std::vector<std::uint64_t>> PieceScheduler::plan_requests(
    const std::vector<std::uint64_t>& missing,
    const std::vector<PeerPieceAvailability>& peers,
    std::size_t max_in_flight_per_peer) const {
  std::map<std::string, std::vector<std::uint64_t>> plan;
  std::map<std::string, std::size_t> in_flight;
  std::set<std::uint64_t> assigned;
  std::vector<std::uint64_t> ordered_missing = missing;

  for (const auto& peer : peers) {
    plan.emplace(peer.peer_key, std::vector<std::uint64_t>{});
    in_flight.emplace(peer.peer_key, 0);
  }

  std::sort(ordered_missing.begin(), ordered_missing.end(), [&](std::uint64_t lhs, std::uint64_t rhs) {
    const std::size_t lhs_count = availability_count(lhs, peers);
    const std::size_t rhs_count = availability_count(rhs, peers);
    if (lhs_count != rhs_count) {
      return lhs_count < rhs_count;
    }
    return lhs < rhs;
  });

  for (const auto piece_index : ordered_missing) {
    if (assigned.count(piece_index) > 0) {
      continue;
    }

    const PeerPieceAvailability* best_peer = nullptr;
    std::tuple<std::size_t, int, std::string> best_score;
    bool has_best = false;

    for (const auto& peer : peers) {
      const auto load_it = in_flight.find(peer.peer_key);
      const std::size_t load = load_it == in_flight.end() ? 0 : load_it->second;
      if (load >= max_in_flight_per_peer) {
        continue;
      }
      if (piece_index >= peer.bitfield.size() || !peer.bitfield[piece_index]) {
        continue;
      }

      const std::tuple<std::size_t, int, std::string> score{load, peer.penalty, peer.peer_key};
      if (!has_best || score < best_score) {
        best_score = score;
        best_peer = &peer;
        has_best = true;
      }
    }

    if (best_peer != nullptr) {
      plan[best_peer->peer_key].push_back(piece_index);
      ++in_flight[best_peer->peer_key];
      assigned.insert(piece_index);
    }
  }

  return plan;
}
