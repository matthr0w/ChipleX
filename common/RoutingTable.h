#pragma once

#include <vector>

class RoutingTable {
public:
  static void initialize(unsigned int num_chiplets);
  static int get_route(unsigned int from, unsigned int to);

private:
  static std::vector<std::vector<int>> table;
};