#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "services/swarm/piece_scheduler.hpp"
#include "services/swarm/swarm_protocol.hpp"

namespace {

std::size_t line_count(const std::string& text) {
  return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
}

}  // namespace

int main() {
  PieceScheduler scheduler;

  const std::vector<std::uint64_t> missing = {0, 1, 2, 3};
  const std::vector<PeerPieceAvailability> peers = {
      {"peer-a", {true, true, false, true}, 0},
      {"peer-b", {false, true, true, false}, 0},
      {"peer-c", {true, false, true, true}, 0},
  };

  const auto plan = scheduler.plan_requests(missing, peers, 2);
  std::vector<std::uint64_t> all_assigned;
  for (const auto& [peer_key, pieces] : plan) {
    (void)peer_key;
    assert(pieces.size() <= 2);
    for (const auto piece : pieces) {
      all_assigned.push_back(piece);
    }
  }
  const std::set<std::uint64_t> unique_assigned(all_assigned.begin(), all_assigned.end());
  std::sort(all_assigned.begin(), all_assigned.end());
  assert(all_assigned.size() == unique_assigned.size());
  assert(std::adjacent_find(all_assigned.begin(), all_assigned.end()) == all_assigned.end());
  assert((all_assigned == std::vector<std::uint64_t>{0, 1, 2, 3}));

  const std::vector<PeerPieceAvailability> rarity_peers = {
      {"peer-a", {true, true}, 0},
      {"peer-b", {true, false}, 0},
  };
  const auto rarity_plan = scheduler.plan_requests({0, 1}, rarity_peers, 1);
  assert(rarity_plan.at("peer-a").size() == 1);
  assert(rarity_plan.at("peer-b").size() == 1);
  assert(rarity_plan.at("peer-a")[0] == 1);
  assert(rarity_plan.at("peer-b")[0] == 0);

  const std::vector<PeerPieceAvailability> penalty_peers = {
      {"peer-a", {true}, 10},
      {"peer-b", {true}, 0},
  };
  const auto penalty_plan = scheduler.plan_requests({0}, penalty_peers, 1);
  assert(penalty_plan.at("peer-a").empty());
  assert(penalty_plan.at("peer-b").size() == 1);
  assert(penalty_plan.at("peer-b")[0] == 0);

  const std::vector<PeerPieceAvailability> symmetric_peers = {
      {"peer-a", {true, true}, 0},
      {"peer-b", {true, true}, 0},
  };
  const auto symmetric_plan = scheduler.plan_requests({0, 1}, symmetric_peers, 2);
  assert(symmetric_plan.at("peer-a").size() == 1);
  assert(symmetric_plan.at("peer-b").size() == 1);
  assert((symmetric_plan.at("peer-a")[0] == 0 || symmetric_plan.at("peer-a")[0] == 1));
  assert((symmetric_plan.at("peer-b")[0] == 0 || symmetric_plan.at("peer-b")[0] == 1));
  assert(symmetric_plan.at("peer-a")[0] != symmetric_plan.at("peer-b")[0]);

  const std::vector<PeerPieceAvailability> counterexample_peers = {
      {"peer-a", {false, true, true}, 0},
      {"peer-b", {true, true, false}, 0},
      {"peer-c", {true, false, false}, 0},
  };
  const auto counterexample_plan = scheduler.plan_requests({0, 1, 2}, counterexample_peers, 2);
  assert(counterexample_plan.at("peer-a").size() == 1);
  assert(counterexample_plan.at("peer-b").size() == 1);
  assert(counterexample_plan.at("peer-c").size() == 1);
  assert(counterexample_plan.at("peer-a")[0] == 2);
  assert(counterexample_plan.at("peer-b")[0] == 1);
  assert(counterexample_plan.at("peer-c")[0] == 0);

  const auto hello = SwarmProtocol::encode_hello("node-a", "192.168.43.10", 8788);
  assert(hello == "SWARM/1\nHELLO\nNODE node-a\nHOST 192.168.43.10\nPORT 8788\n");

  const auto manifest = SwarmProtocol::encode_manifest_request("torrent-a");
  assert(manifest == "SWARM/1\nMANIFEST_REQUEST\nTORRENT torrent-a\n");

  const auto piece = SwarmProtocol::encode_piece_request("torrent-a", 42);
  assert(piece == "SWARM/1\nPIECE_REQUEST\nTORRENT torrent-a\nPIECE 42\n");

  const auto injected_hello = SwarmProtocol::encode_hello("node-a\nINJECT", "192.168.43.10\rBAD", 8788);
  assert(line_count(injected_hello) == 5);
  assert(injected_hello.find("INJECT") != std::string::npos);
  assert(injected_hello.find("BAD") != std::string::npos);
  assert(injected_hello.find('\r') == std::string::npos);

  const auto injected_manifest = SwarmProtocol::encode_manifest_request("torrent-a\nEXTRA");
  assert(line_count(injected_manifest) == 3);
  assert(injected_manifest.find("EXTRA") != std::string::npos);
  assert(injected_manifest.find('\r') == std::string::npos);

  const auto injected_piece = SwarmProtocol::encode_piece_request("torrent-a\rMORE", 7);
  assert(line_count(injected_piece) == 4);
  assert(injected_piece.find("MORE") != std::string::npos);
  assert(injected_piece.find('\r') == std::string::npos);

  const std::vector<PeerPieceAvailability> matching_peers = {
      {"peer-a", {true, false, true, true}, 0},
      {"peer-b", {false, true, true, true}, 0},
      {"peer-c", {true, false, false, false}, 0},
      {"peer-d", {false, true, false, false}, 0},
  };
  const auto matching_plan = scheduler.plan_requests({0, 1, 2, 3}, matching_peers, 1);
  std::vector<std::uint64_t> matching_assigned;
  for (const auto& [peer_key, pieces] : matching_plan) {
    (void)peer_key;
    assert(pieces.size() <= 1);
    matching_assigned.insert(matching_assigned.end(), pieces.begin(), pieces.end());
  }
  std::sort(matching_assigned.begin(), matching_assigned.end());
  assert((matching_assigned == std::vector<std::uint64_t>{0, 1, 2, 3}));

  return 0;
}
