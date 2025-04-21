#include "RoutingTable.h"

std::vector<std::vector<int>> RoutingTable::table;

void RoutingTable::initialize(unsigned int num_chiplets) {
  table.resize(num_chiplets, std::vector<int>(num_chiplets));

  if (num_chiplets == 2) {
    table[0][1] = 1;
    table[1][0] = 0;
    return;
  }

  for (unsigned int from = 0; from < num_chiplets; ++from) {
    for (unsigned int to = 0; to < num_chiplets; ++to) {
      if (from == to)
        continue;

      int cw_dist = (to - from + num_chiplets) % num_chiplets;
      int ccw_dist = (from - to + num_chiplets) % num_chiplets;

      // clockwise direction -> interconnect1
      // counter-clockwise direction -> interconnect0
      table[from][to] = (cw_dist <= ccw_dist) ? 1 : 0;
    }
  }
}

int RoutingTable::get_route(unsigned int from, unsigned int to) {
  return table[from][to];
}