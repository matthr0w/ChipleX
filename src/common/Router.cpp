#include "common/Router.h"

#include <queue>

void Router::build_table() {
  unsigned num_chiplets = static_cast<int>(sysconf_.chiplets.size());
  routing_table_.assign(num_chiplets, std::vector<RouteInfo>(num_chiplets));

  // Build adjacency list
  std::unordered_map<int, std::vector<std::tuple<int, int, int>>> adj;
  // (neighbor_chiplet_id, interconnect_id, link_id)
  for (const auto &conn : sysconf_.connections) {
    adj[conn.endpoint0.chiplet_id].push_back({conn.endpoint1.chiplet_id,
                                              conn.endpoint0.interconnect_id,
                                              conn.endpoint0.link_id});

    adj[conn.endpoint1.chiplet_id].push_back({conn.endpoint0.chiplet_id,
                                              conn.endpoint1.interconnect_id,
                                              conn.endpoint1.link_id});
  }

  // Run BFS from each source
  for (int src = 0; src < num_chiplets; ++src) {
    std::queue<std::tuple<int, int, int>> q;
    // (current_chiplet_id, first_hop_interconnect_id, first_hop_link_id)

    std::vector<bool> visited(num_chiplets, false);

    visited[src] = true;
    q.push({src, -1, -1});

    while (!q.empty()) {
      auto [current, first_hop_interconnect, first_hop_link] = q.front();
      q.pop();

      for (auto &[neighbor, interconnect_id, link_id] : adj[current]) {
        if (visited[neighbor])
          continue;
        visited[neighbor] = true;

        int hop_interconnect =
            (current == src) ? interconnect_id : first_hop_interconnect;
        int hop_link = (current == src) ? link_id : first_hop_link;

        routing_table_[src][neighbor] = {hop_interconnect, hop_link};
        q.push({neighbor, hop_interconnect, hop_link});
      }
    }
  }
}

int Router::get_link_id(int src_id, int interconnect_id, int dst_id) const {
  if (src_id < 0 || src_id >= (int)routing_table_.size() || dst_id < 0 ||
      dst_id >= (int)routing_table_[src_id].size())
    return -1;

  const auto &route = routing_table_[src_id][dst_id];
  if (route.interconnect_id == interconnect_id)
    return route.link_id;
  return -1;
}

int Router::get_dest_id(int src_id, int interconnect_id, int link_id) const {
  for (const auto &conn : sysconf_.connections) {
    // Case 1: source is endpoint0
    if (conn.endpoint0.chiplet_id == src_id &&
        conn.endpoint0.interconnect_id == interconnect_id &&
        conn.endpoint0.link_id == link_id)
      return conn.endpoint1.chiplet_id;

    // Case 2: source is endpoint1
    if (conn.endpoint1.chiplet_id == src_id &&
        conn.endpoint1.interconnect_id == interconnect_id &&
        conn.endpoint1.link_id == link_id)
      return conn.endpoint0.chiplet_id;
  }
  return -1;
}