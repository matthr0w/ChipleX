#include "RoutingTable.h"

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
  return table[from][to];
}