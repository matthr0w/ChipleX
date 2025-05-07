#include "RoutingTable.h"

#include <algorithm>

#include "include/globals.h"

std::vector<std::vector<int>> RoutingTable::table;

void RoutingTable::initialize(unsigned int num_chiplets) {
  table.resize(num_chiplets + 1, std::vector<int>(num_chiplets + 1));

  if (num_chiplets == 2) {
    table[1][2] = 2;
    table[2][1] = 1;
    return;
  }

  for (unsigned int from = 1; from < num_chiplets + 1; ++from) {
    for (unsigned int to = 1; to < num_chiplets + 1; ++to) {
      if (from == to)
        continue;

      int cw_dist = (to - from + num_chiplets) % num_chiplets;
      int ccw_dist = (from - to + num_chiplets) % num_chiplets;

      // clockwise direction -> interconnect2
      // counter-clockwise direction -> interconnect1
      table[from][to] = (cw_dist <= ccw_dist) ? 2 : 1;
    }
  }
}

int RoutingTable::get_route(unsigned int from, unsigned int to) {
  if (from == to)
    return -1;

  // FPGA is source
  if (from == 0) {
    if (connections.empty())
      return -1;

    int min_dist = num_chiplets + 1;
    int closest_chiplet = -1;

    for (unsigned int conn : connections) {
      int cw_dist = (to - conn + num_chiplets) % num_chiplets;
      int ccw_dist = (conn - to + num_chiplets) % num_chiplets;
      int dist = std::min(cw_dist, ccw_dist);

      if (dist < min_dist) {
        min_dist = dist;
        closest_chiplet = conn;
      }
    }

    return closest_chiplet;
  }

  // FPGA is destination
  if (to == 0) {
    if (connections.empty())
      return -1;

    // source has connection to FPGA -> Interconnect0
    if (std::find(connections.begin(), connections.end(), from) !=
        connections.end()) {
      return 0;
    }

    int min_dist = num_chiplets + 1;
    int closest_chiplet = -1;

    for (unsigned int conn : connections) {
      int cw_dist = (conn - from + num_chiplets) % num_chiplets;
      int ccw_dist = (from - conn + num_chiplets) % num_chiplets;
      int dist = std::min(cw_dist, ccw_dist);

      if (dist < min_dist) {
        min_dist = dist;
        closest_chiplet = conn;
      }
    }

    return table[from][closest_chiplet];
  }

  // chiplet-to-chiplet routing
  return table[from][to];
}