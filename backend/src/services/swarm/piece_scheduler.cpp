#include "services/swarm/piece_scheduler.hpp"

#include <algorithm>
#include <cstddef>
#include <string>

namespace {

struct SlotInfo {
  std::size_t peer_index;
  std::size_t ordinal;
  int penalty;
  std::string peer_key;
};

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
  std::vector<std::uint64_t> ordered_missing = missing;
  std::vector<SlotInfo> slots;

  for (std::size_t peer_index = 0; peer_index < peers.size(); ++peer_index) {
    const auto& peer = peers[peer_index];
    plan.emplace(peer.peer_key, std::vector<std::uint64_t>{});
    for (std::size_t ordinal = 0; ordinal < max_in_flight_per_peer; ++ordinal) {
      slots.push_back(SlotInfo{peer_index, ordinal, peer.penalty, peer.peer_key});
    }
  }

  std::sort(ordered_missing.begin(), ordered_missing.end(), [&](std::uint64_t lhs, std::uint64_t rhs) {
    const std::size_t lhs_count = availability_count(lhs, peers);
    const std::size_t rhs_count = availability_count(rhs, peers);
    if (lhs_count != rhs_count) {
      return lhs_count < rhs_count;
    }
    return lhs < rhs;
  });
  ordered_missing.erase(std::unique(ordered_missing.begin(), ordered_missing.end()), ordered_missing.end());

  std::vector<std::vector<std::size_t>> adjacency(ordered_missing.size());
  for (std::size_t piece_pos = 0; piece_pos < ordered_missing.size(); ++piece_pos) {
    const auto piece_index = ordered_missing[piece_pos];
    for (std::size_t slot_index = 0; slot_index < slots.size(); ++slot_index) {
      const auto& slot = slots[slot_index];
      const auto& peer = peers[slot.peer_index];
      if (piece_index < peer.bitfield.size() && peer.bitfield[piece_index]) {
        adjacency[piece_pos].push_back(slot_index);
      }
    }

    std::sort(adjacency[piece_pos].begin(), adjacency[piece_pos].end(), [&](std::size_t lhs, std::size_t rhs) {
      const auto& left = slots[lhs];
      const auto& right = slots[rhs];
      if (left.penalty != right.penalty) {
        return left.penalty < right.penalty;
      }
      if (left.peer_key != right.peer_key) {
        return left.peer_key < right.peer_key;
      }
      return left.ordinal < right.ordinal;
    });
  }

  const std::size_t unmatched = ordered_missing.size();
  std::vector<std::size_t> slot_match(slots.size(), unmatched);

  const auto augment = [&](auto&& self, std::size_t piece_pos, std::vector<char>& seen) -> bool {
    for (const auto slot_index : adjacency[piece_pos]) {
      if (seen[slot_index] != 0) {
        continue;
      }
      seen[slot_index] = 1;

      const std::size_t matched_piece = slot_match[slot_index];
      if (matched_piece == unmatched || self(self, matched_piece, seen)) {
        slot_match[slot_index] = piece_pos;
        return true;
      }
    }
    return false;
  };

  for (std::size_t piece_pos = 0; piece_pos < ordered_missing.size(); ++piece_pos) {
    std::vector<char> seen(slots.size(), 0);
    (void)augment(augment, piece_pos, seen);
  }

  for (std::size_t slot_index = 0; slot_index < slots.size(); ++slot_index) {
    const std::size_t piece_pos = slot_match[slot_index];
    if (piece_pos == unmatched) {
      continue;
    }
    plan[slots[slot_index].peer_key].push_back(ordered_missing[piece_pos]);
  }

  for (auto& [peer_key, pieces] : plan) {
    (void)peer_key;
    std::sort(pieces.begin(), pieces.end());
  }

  return plan;
}
