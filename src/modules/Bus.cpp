#include "modules/Bus.h"

#include <unordered_set>

#include "logging.h"

#include "common/AxiTrace.h"
#include "common/Router.h"
#include "modules/chiplets/ChipletRegistry.h"

Bus::Bus(sc_module_name name, unsigned chiplet_id, ChipletConfig chiplet_config, unsigned num_managers,
         unsigned num_subordinates)
    : sc_module(name),
      chiplet_id(chiplet_id),
      axi_width(chiplet_config.node["axi"]["width"].as<unsigned>()),
      clk_cycle(chiplet_config.node["axi"]["clk_cycle"].as<unsigned>(), SC_NS),
      beat_data(new uint8_t[axi_width >> 3]) {
	for (unsigned i = 0; i < num_managers; ++i) {
		for (unsigned j = 0; j < num_subordinates; ++j) {
			std::string mgr_name  = ChipletRegistry::instance().get_module_name_at_mgr_port(chiplet_id, i);
			std::string sub_name  = ChipletRegistry::instance().get_module_name_at_sub_port(chiplet_id, j);
			std::string util_name = "utilization_" + mgr_name + "<->" + sub_name;
			stats.register_utilization(this->name(), util_name, clk_cycle);
			mgr_names[i] = mgr_name;
			sub_names[j] = sub_name;
		}
	}

	interconnect_names = chiplet_config.interconnect_ids_reverse;

	managers.reserve(num_managers);
	for (unsigned i = 0; i < num_managers; ++i) {
		std::ostringstream name;
		name << "tsocket" << i;
		managers.emplace_back(std::make_unique<ARM::AXI4::SimpleTargetSocketTagged<Bus>>(
		    name.str().c_str(), *this, i, &Bus::nb_transport_fw, ARM::TLM::PROTOCOL_AXI4, axi_width));
	}

	subordinates.reserve(num_subordinates);
	for (unsigned i = 0; i < num_subordinates; ++i) {
		std::ostringstream name;
		name << "isocket" << i;
		subordinates.emplace_back(std::make_unique<ARM::AXI4::SimpleInitiatorSocketTagged<Bus>>(
		    name.str().c_str(), *this, i, &Bus::nb_transport_bw, ARM::TLM::PROTOCOL_AXI4, axi_width));
	}

	SC_METHOD(clk_posedge);
	sensitive << clk.pos();
	dont_initialize();
}

Bus::~Bus() {
	delete[] beat_data;
}

void Bus::clk_posedge() {
	std::unordered_set<unsigned> sub_used;
	std::unordered_set<unsigned> mgr_used;

	for (auto &[key, conn] : connections) {
		auto [mgr_id, sub_id] = key;
		std::string util_name = "utilization_" + mgr_names[mgr_id] + "<->" + sub_names[sub_id];

		// Forward direction
		if (!conn.fw_q.empty() && !sub_used.count(sub_id)) {
			sub_used.insert(sub_id);

			auto request = conn.fw_q.front();
			conn.fw_q.pop_front();

			ARM::AXI::Phase prev_phase = request.phase;
			auto            reply      = subordinates[sub_id]->nb_transport_fw(*request.payload, request.phase);

			if (reply == TLM_UPDATED) {
				conn.bw_q.push_back({request.payload, request.phase});
			}

			stats.mark_active_cycle(this->name(), util_name);

			print_payload(*request.payload, prev_phase, request.phase);

			stats.end_cycle(this->name(), util_name);
		}

		// Backward direction
		if (!conn.bw_q.empty() && !mgr_used.count(mgr_id)) {
			mgr_used.insert(mgr_id);

			auto request = conn.bw_q.front();
			conn.bw_q.pop_front();

			ARM::AXI::Phase prev_phase = request.phase;
			auto            reply      = managers[mgr_id]->nb_transport_bw(*request.payload, request.phase);

			if (reply == TLM_UPDATED) {
				conn.fw_q.push_back({request.payload, request.phase});
			}

			stats.mark_active_cycle(this->name(), util_name);

			print_payload(*request.payload, prev_phase, request.phase);

			stats.end_cycle(this->name(), util_name);
		}
	}
}

// -------------------------------------------------------
// Transport Functions
// -------------------------------------------------------
tlm_sync_enum Bus::nb_transport_fw(int mgr_id, ARM::AXI::Payload &payload, ARM::AXI::Phase &phase) {
	int sub_id = 0;
	if (auto it = payloads2sub.find(&payload); it != payloads2sub.end()) {
		sub_id = it->second;
	} else {
		UserSignals user = UserSignals::decode(payload.user);
		if (user.src_chiplet == chiplet_id) {
			sub_id = ChipletRegistry::instance().get(chiplet_id)->get_sub_port(user.src_module);
		} else if (user.dst_chiplet == chiplet_id) {
			sub_id = ChipletRegistry::instance().get(chiplet_id)->get_sub_port(user.dst_module);
		} else {
			int         id   = Router::instance().get_interconnect_id(chiplet_id, user.dst_chiplet);
			std::string name = interconnect_names.find(id)->second;
			sub_id           = ChipletRegistry::instance().get(chiplet_id)->get_sub_port(name);
		}
		payloads2sub[&payload] = sub_id;
		payloads2mgr[&payload] = mgr_id;
	}

	auto &conn = connections[{mgr_id, sub_id}];
	conn.fw_q.push_back({&payload, phase});

	return TLM_ACCEPTED;
}

tlm_sync_enum Bus::nb_transport_bw(int sub_id, ARM::AXI::Payload &payload, ARM::AXI::Phase &phase) {
	int mgr_id = 0;
	if (auto it = payloads2mgr.find(&payload); it != payloads2mgr.end()) {
		mgr_id = it->second;
	} else {
		SC_LOG_WARN(this, "Unknown backward response from SUB[" << sub_id << "]");
		return TLM_ACCEPTED;
	}

	auto &conn = connections[{mgr_id, sub_id}];
	conn.bw_q.push_back({&payload, phase});

	return TLM_ACCEPTED;
}

// -------------------------------------------------------
// Debug Functions
// -------------------------------------------------------
void Bus::print_payload(ARM::AXI::Payload &payload, ARM::AXI::Phase phase, ARM::AXI::Phase) {
	const char *phase_name = "?";
	bool        show_addr  = true;
	bool        show_data  = false;
	bool        show_resp  = false;
	bool        inc_beat   = false;
	bool        first_beat = false;
	bool        last_beat  = false;

	bool updated = false;

	auto it = payload_phase_map.find(&payload);
	if (it != payload_phase_map.end()) {
		ARM::AXI::Phase prev_phase = it->second;

		auto check_transition = [&](auto valid_fn, auto ready_fn) { return (valid_fn(prev_phase) && ready_fn(phase)); };

		if (check_transition(is_aw_valid, is_aw_ready) || check_transition(is_w_valid, is_w_ready) ||
		    check_transition(is_w_valid_last, is_w_ready) || check_transition(is_b_valid, is_b_ready) ||
		    check_transition(is_ar_valid, is_ar_ready) || check_transition(is_r_valid, is_r_ready) ||
		    check_transition(is_r_valid_last, is_r_ready)) {
			updated = true;
		}

		it->second = phase;
	} else {
		payload_phase_map[&payload] = phase;
	}

	switch (phase) {
	case ARM::AXI::PHASE_UNINITIALIZED:
		phase_name = "PHASE_UNINITIALIZED";
		show_addr  = false;
		break;
	case ARM::AXI::AW_VALID:
		phase_name = (updated ? "AW VALID READY" : "AW VALID -----");
		break;
	case ARM::AXI::AW_READY:
		phase_name = (updated ? "AW VALID READY" : "AW ----- READY");
		break;
	case ARM::AXI::W_VALID:
	case ARM::AXI::W_VALID_LAST:
		phase_name = (updated ? "W  VALID READY" : "W  VALID -----");
		inc_beat   = updated;
		show_data  = true;
		first_beat = true;
		last_beat  = updated;
		break;
	case ARM::AXI::W_READY:
		inc_beat   = true;
		phase_name = (updated ? "W  VALID READY" : "W  ----- READY");
		show_data  = true;
		last_beat  = true;
		break;
	case ARM::AXI::B_VALID:
		phase_name = (updated ? "B  VALID READY" : "B  VALID -----");
		show_resp  = true;
		last_beat  = updated;
		break;
	case ARM::AXI::B_READY:
		phase_name = (updated ? "B  VALID READY" : "B  ----- READY");
		show_resp  = true;
		last_beat  = true;
		break;
	case ARM::AXI::AR_VALID:
		phase_name = (updated ? "AR VALID READY" : "AR VALID -----");
		break;
	case ARM::AXI::AR_READY:
		phase_name = (updated ? "AR VALID READY" : "AR ----- READY");
		break;
	case ARM::AXI::R_VALID:
	case ARM::AXI::R_VALID_LAST:
		phase_name = (updated ? "R  VALID READY" : "R  VALID -----");
		inc_beat   = updated;
		show_data  = true;
		show_resp  = true;
		first_beat = true;
		last_beat  = updated;
		break;
	case ARM::AXI::R_READY:
		inc_beat   = true;
		phase_name = (updated ? "R  VALID READY" : "R  ----- READY");
		show_data  = true;
		show_resp  = true;
		last_beat  = true;
		break;
	default:
		show_addr = false;
		break;
	}

	// Track beats
	if (first_beat && payload_burst_index.find(&payload) == payload_burst_index.end()) {
		payload_burst_index[&payload] = 0;
	}

	std::ostringstream message;

	// Channel source/destination info
	auto mgr_it = payloads2mgr.find(&payload);
	auto sub_it = payloads2sub.find(&payload);
	if (mgr_it != payloads2mgr.end() && sub_it != payloads2sub.end()) {
		message << std::left << std::setw(10) << mgr_names[mgr_it->second] << " <-> " << std::setw(10)
		        << sub_names[sub_it->second] << " | ";
	}

	// Phase
	message << phase_name << " ";

	if (show_addr) {
		message << AxiTrace::addressing(payload);
	}

	if (show_resp) {
		message << AxiTrace::resp_name(payload.get_resp()) << ' ';
	} else {
		message << "       ";
	}

	if (show_data) {
		unsigned burst_index = payload_burst_index[&payload];
		message << (burst_index == payload.get_len() ? "| LAST " : "| ") << "DATA:"
		        << AxiTrace::beat_dump(payload, burst_index, beat_data) << ' ';

		if (inc_beat) {
			payload_burst_index[&payload] = burst_index + 1;
		}
	}

	message << "| " << AxiTrace::identity(payload) << ' ' << AxiTrace::attributes(payload);

	if (last_beat) {
		payload_phase_map.erase(&payload);
	}

	if (last_beat && payload_burst_index[&payload] == payload.get_beat_count()) {
		payload_burst_index.erase(&payload);
	}

	SC_LOG_DEBUG(this, message.str());
}