#pragma once

#include <cstdint>
#include <unordered_map>

#include "setup/Types.h"

class Router {
  public:
	Router(const Router &)            = delete;
	Router &operator=(const Router &) = delete;

	static Router &instance() {
		static Router instance_;
		return instance_;
	}

	void init(const SystemConfig &sysconf) {
		sysconf_ = sysconf;
		build_table();
	}

	int get_interconnect_id(int src_id, int dst_id) const;
	int get_link_id(int src_id, int interconnect_id, int dst_id) const;
	int get_dest_id(int src_id, int interconnect_id, int link_id) const;

  private:
	Router() = default;

	SystemConfig sysconf_;

	struct RouteInfo {
		int interconnect_id = -1;
		int link_id         = -1;
	};

	std::vector<std::vector<RouteInfo>> routing_table_;

	// O(1) reverse lookup for get_dest_id: (src, interconnect, link) -> dest.
	// Ids are small config-derived values; 10 bits each is ample.
	static uint32_t encode_link(int src_id, int interconnect_id, int link_id) {
		return (static_cast<uint32_t>(src_id & 0x3FF) << 20) | (static_cast<uint32_t>(interconnect_id & 0x3FF) << 10) |
		       static_cast<uint32_t>(link_id & 0x3FF);
	}

	std::unordered_map<uint32_t, int> dest_table_;

	void build_table();
};
