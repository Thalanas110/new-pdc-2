#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct PeerPieceAvailability {
  std::string peer_key;
  std::vector<bool> bitfield;
  int penalty = 0;
};

class PieceScheduler {
 public:
  std::map<std::string, std::vector<std::uint64_t>> plan_requests(
      const std::vector<std::uint64_t>& missing,
      const std::vector<PeerPieceAvailability>& peers,
      std::size_t max_in_flight_per_peer) const;
};
