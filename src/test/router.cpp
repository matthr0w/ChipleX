#include "router.h"

#include <iostream>
#include <queue>

void Router::build_table() {
  int n = static_cast<int>(sysconf_.chiplet_order.size());
  routing_table_.assign(n, std::vector<int>(n, -1));

  // Build adjacency list
  std::unordered_map<int, std::vector<std::pair<int, int>>> adj;
  for (const auto &conn : sysconf_.interconnect.connections) {
    adj[conn.endpoint0.chiplet_id].push_back(
        {conn.endpoint1.chiplet_id, conn.endpoint0.link_id});
    adj[conn.endpoint1.chiplet_id].push_back(
        {conn.endpoint0.chiplet_id, conn.endpoint1.link_id});
  }

  // Run BFS from each source
  for (int src = 0; src < n; ++src) {
    std::queue<std::pair<int, int>> q;
    std::vector<bool> visited(n, false);

    visited[src] = true;
    q.push({src, -1});

    while (!q.empty()) {
      auto [current, first_hop] = q.front();
      q.pop();

      for (auto &[neighbor, link_id] : adj[current]) {
        if (visited[neighbor])
          continue;
        visited[neighbor] = true;

        // If we are leaving the source, this is the first hop
        int hop_to_use = (current == src) ? link_id : first_hop;

        routing_table_[src][neighbor] = hop_to_use;
        q.push({neighbor, hop_to_use});
      }
    }
  }
}

void Router::print_table() const {
  std::cout << "================ ROUTING TABLE ================\n";

  int n = static_cast<int>(routing_table_.size());

  std::cout << "rows=src, cols=dst\n";
  std::cout << "     ";
  for (int j = 0; j < n; ++j) {
    std::cout << "[" << j << "]  ";
  }
  std::cout << "\n";

  for (int i = 0; i < n; ++i) {
    std::cout << "[" << i << "] ";
    for (int j = 0; j < n; ++j) {
      int val = routing_table_[i][j];
      if (val == -1)
        std::cout << "  -  ";
      else
        std::cout << "  " << val << "  ";
    }
    std::cout << "\n";
  }

  std::cout << "===============================================\n";
}

int Router::get_link_id(int src_id, int dst_id) const {
  if (src_id < 0 || src_id >= (int)routing_table_.size() || dst_id < 0 ||
      dst_id >= (int)routing_table_[src_id].size())
    return -1; // Out of bounds

  return routing_table_[src_id][dst_id];
}

int Router::get_dest_id(int src_id, int link_id) const {
  for (const auto &conn : sysconf_.interconnect.connections) {
    // Case 1: source is endpoint0
    if (conn.endpoint0.chiplet_id == src_id &&
        conn.endpoint0.link_id == link_id)
      return conn.endpoint1.chiplet_id;

    // Case 2: source is endpoint1
    if (conn.endpoint1.chiplet_id == src_id &&
        conn.endpoint1.link_id == link_id)
      return conn.endpoint0.chiplet_id;
  }

  return -1;
}