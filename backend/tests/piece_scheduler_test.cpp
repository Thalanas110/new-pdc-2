#include <cassert>
#include <cstdint>
#include <map>
#include <vector>

#include "services/swarm/piece_scheduler.hpp"
#include "services/swarm/swarm_protocol.hpp"

int main() {
  PieceScheduler scheduler;

  const std::vector<std::uint64_t> missing = {0, 1, 2, 3};
  const std::vector<PeerPieceAvailability> peers = {
      {"peer-a", {true, true, false, true}, 0},
      {"peer-b", {false, true, true, false}, 0},
      {"peer-c", {true, false, true, true}, 0},
  };

  const auto plan = scheduler.plan_requests(missing, peers, 2);
  assert(plan.at("peer-a").size() <= 2);
  assert(plan.at("peer-b").size() <= 2);
  assert(plan.at("peer-c").size() <= 2);

  const auto hello = SwarmProtocol::encode_hello("node-a", "192.168.43.10", 8788);
  assert(hello.rfind("SWARM/1\nHELLO\n", 0) == 0);

  return 0;
}
