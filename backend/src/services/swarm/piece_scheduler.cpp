#include "services/swarm/piece_scheduler.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

struct SlotInfo {
  std::size_t peer_index;
  std::size_t ordinal;
  std::string peer_key;
};

struct FlowEdge {
  std::size_t to;
  std::size_t reverse_index;
  int capacity;
  std::int64_t cost;
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

std::size_t peer_missing_count(
    const PeerPieceAvailability& peer, const std::vector<std::uint64_t>& missing_pieces) {
  std::size_t count = 0;
  for (const auto piece_index : missing_pieces) {
    if (piece_index < peer.bitfield.size() && peer.bitfield[piece_index]) {
      ++count;
    }
  }
  return count;
}

class MinCostMaxFlow {
 public:
  explicit MinCostMaxFlow(std::size_t node_count) : graph_(node_count) {}

  void add_edge(std::size_t from, std::size_t to, int capacity, std::int64_t cost) {
    const FlowEdge forward{to, graph_[to].size(), capacity, cost};
    const FlowEdge reverse{from, graph_[from].size(), 0, -cost};
    graph_[from].push_back(forward);
    graph_[to].push_back(reverse);
  }

  std::pair<int, std::int64_t> solve(std::size_t source, std::size_t sink) {
    int total_flow = 0;
    std::int64_t total_cost = 0;
    const std::size_t node_count = graph_.size();
    const std::int64_t inf = std::numeric_limits<std::int64_t>::max() / 4;

    while (true) {
      std::vector<std::int64_t> dist(node_count, inf);
      std::vector<std::size_t> prev_node(node_count, node_count);
      std::vector<std::size_t> prev_edge(node_count, 0);
      std::vector<bool> in_queue(node_count, false);
      std::deque<std::size_t> queue;

      dist[source] = 0;
      queue.push_back(source);
      in_queue[source] = true;

      while (!queue.empty()) {
        const auto node = queue.front();
        queue.pop_front();
        in_queue[node] = false;

        for (std::size_t edge_index = 0; edge_index < graph_[node].size(); ++edge_index) {
          const auto& edge = graph_[node][edge_index];
          if (edge.capacity == 0 || dist[node] == inf) {
            continue;
          }

          const auto next_dist = dist[node] + edge.cost;
          if (next_dist >= dist[edge.to]) {
            continue;
          }

          dist[edge.to] = next_dist;
          prev_node[edge.to] = node;
          prev_edge[edge.to] = edge_index;
          if (!in_queue[edge.to]) {
            queue.push_back(edge.to);
            in_queue[edge.to] = true;
          }
        }
      }

      if (dist[sink] == inf) {
        break;
      }

      int augment = std::numeric_limits<int>::max();
      for (std::size_t node = sink; node != source; node = prev_node[node]) {
        const auto& edge = graph_[prev_node[node]][prev_edge[node]];
        augment = std::min(augment, edge.capacity);
      }

      for (std::size_t node = sink; node != source; node = prev_node[node]) {
        auto& edge = graph_[prev_node[node]][prev_edge[node]];
        auto& reverse = graph_[edge.to][edge.reverse_index];
        edge.capacity -= augment;
        reverse.capacity += augment;
      }

      total_flow += augment;
      total_cost += dist[sink] * augment;
    }

    return {total_flow, total_cost};
  }

  const std::vector<std::vector<FlowEdge>>& graph() const { return graph_; }

 private:
  std::vector<std::vector<FlowEdge>> graph_;
};

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
      slots.push_back(SlotInfo{peer_index, ordinal, peer.peer_key});
    }
  }

  std::sort(ordered_missing.begin(), ordered_missing.end());
  ordered_missing.erase(std::unique(ordered_missing.begin(), ordered_missing.end()), ordered_missing.end());
  std::stable_sort(ordered_missing.begin(), ordered_missing.end(), [&](std::uint64_t lhs, std::uint64_t rhs) {
    const std::size_t lhs_count = availability_count(lhs, peers);
    const std::size_t rhs_count = availability_count(rhs, peers);
    if (lhs_count != rhs_count) {
      return lhs_count < rhs_count;
    }
    return lhs < rhs;
  });

  if (ordered_missing.empty() || slots.empty()) {
    return plan;
  }

  std::vector<std::size_t> peer_piece_counts(peers.size(), 0);
  for (std::size_t peer_index = 0; peer_index < peers.size(); ++peer_index) {
    peer_piece_counts[peer_index] = peer_missing_count(peers[peer_index], ordered_missing);
  }

  const auto max_assignments = static_cast<std::int64_t>(std::min(ordered_missing.size(), slots.size()));
  const auto flex_bound = max_assignments * static_cast<std::int64_t>(ordered_missing.size());
  const auto peer_tiebreak_bound = max_assignments * static_cast<std::int64_t>(peers.size());
  const auto spread_bound =
      max_assignments *
      static_cast<std::int64_t>(max_in_flight_per_peer == 0 ? 0 : max_in_flight_per_peer - 1);
  const std::int64_t spread_weight = flex_bound + peer_tiebreak_bound + 1;
  const std::int64_t penalty_weight =
      spread_bound * spread_weight + flex_bound + peer_tiebreak_bound + 1;

  const std::size_t source = 0;
  const std::size_t piece_base = 1;
  const std::size_t slot_base = piece_base + ordered_missing.size();
  const std::size_t sink = slot_base + slots.size();
  MinCostMaxFlow flow(sink + 1);

  for (std::size_t piece_pos = 0; piece_pos < ordered_missing.size(); ++piece_pos) {
    flow.add_edge(source, piece_base + piece_pos, 1, 0);
  }

  for (std::size_t slot_index = 0; slot_index < slots.size(); ++slot_index) {
    const auto& slot = slots[slot_index];
    const std::int64_t slot_cost = static_cast<std::int64_t>(slot.ordinal) * spread_weight;
    flow.add_edge(slot_base + slot_index, sink, 1, slot_cost);
  }

  for (std::size_t piece_pos = 0; piece_pos < ordered_missing.size(); ++piece_pos) {
    const auto piece_index = ordered_missing[piece_pos];
    std::vector<std::size_t> candidate_slots;
    for (std::size_t slot_index = 0; slot_index < slots.size(); ++slot_index) {
      const auto& slot = slots[slot_index];
      const auto& peer = peers[slot.peer_index];
      if (piece_index < peer.bitfield.size() && peer.bitfield[piece_index]) {
        candidate_slots.push_back(slot_index);
      }
    }

    std::sort(candidate_slots.begin(), candidate_slots.end(), [&](std::size_t lhs, std::size_t rhs) {
      const auto& left = slots[lhs];
      const auto& right = slots[rhs];
      const auto& left_peer = peers[left.peer_index];
      const auto& right_peer = peers[right.peer_index];
      if (left_peer.penalty != right_peer.penalty) {
        return left_peer.penalty < right_peer.penalty;
      }
      if (peer_piece_counts[left.peer_index] != peer_piece_counts[right.peer_index]) {
        return peer_piece_counts[left.peer_index] < peer_piece_counts[right.peer_index];
      }
      if (left.peer_key != right.peer_key) {
        return left.peer_key < right.peer_key;
      }
      return left.ordinal < right.ordinal;
    });

    for (const auto slot_index : candidate_slots) {
      const auto& slot = slots[slot_index];
      const auto& peer = peers[slot.peer_index];
      const std::int64_t edge_cost =
          static_cast<std::int64_t>(peer.penalty) * penalty_weight +
          static_cast<std::int64_t>(peer_piece_counts[slot.peer_index]) +
          static_cast<std::int64_t>(slot.peer_index);
      flow.add_edge(piece_base + piece_pos, slot_base + slot_index, 1, edge_cost);
    }
  }

  (void)flow.solve(source, sink);

  const auto& graph = flow.graph();
  for (std::size_t piece_pos = 0; piece_pos < ordered_missing.size(); ++piece_pos) {
    const auto piece_node = piece_base + piece_pos;
    for (const auto& edge : graph[piece_node]) {
      if (edge.to < slot_base || edge.to >= sink || edge.capacity != 0) {
        continue;
      }

      const auto slot_index = edge.to - slot_base;
      plan[slots[slot_index].peer_key].push_back(ordered_missing[piece_pos]);
      break;
    }
  }

  for (auto& [peer_key, pieces] : plan) {
    (void)peer_key;
    std::sort(pieces.begin(), pieces.end());
  }

  return plan;
}
