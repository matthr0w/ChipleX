#include "modules/interconnects/serial_link/NetworkLayer.h"

#include "common/RoutingTable.h"
#include "logging.h"

SLNetworkLayer::SLNetworkLayer(sc_module_name name, unsigned chip_id,
                               unsigned axi_width, unsigned num_interconnects,
                               unsigned num_credits)
    : sc_module(name), chip_id(chip_id), axi_width(axi_width),
      num_credits(num_credits), force_send_thresh(num_credits - 4),
      axi_in("axi_in", *this, &SLNetworkLayer::nb_transport_fw,
             ARM::TLM::PROTOCOL_AXI4, axi_width),
      axi_out("axi_out", *this, &SLNetworkLayer::nb_transport_bw,
              ARM::TLM::PROTOCOL_AXI4, axi_width) {
  // Initial values
  committer_state_q.write(Committer::Idle);
  committer_state_d.write(Committer::Idle);
  entropy_q.write(false);
  entropy_d.write(false);
  axis_reg_valid_in.write(false);
  axis_reg_ready_in.write(false);

  credits_out.resize(num_interconnects, num_credits);
  credits_to_send.resize(num_interconnects, 0);
  credit_to_send_force.resize(num_interconnects, false);

  SC_THREAD(clk_posedge);
  dont_initialize();
  sensitive << clk.pos();

  SC_THREAD(committer_thread);
  sensitive << update_event << aw_gnt << ar_gnt << axi_in_sig << axi_out_sig
            << committer_state_q;

  SC_THREAD(sender_thread);
  sensitive << update_event << aw_gnt << w_gnt << b_gnt << ar_gnt << r_gnt
            << axis_reg_valid_in << axis_reg_ready_in;
}

void SLNetworkLayer::clk_posedge() {
  while (true) {
    wait();

    // Update registers
    committer_state_q.write(committer_state_d.read());
    // TODO: Introduce some randomness
    entropy_q.write(entropy_d.read());

    clear_axi_state();

    if (axis_reg_valid_in.read() && axis_reg_ready_in.read()) {
      // Respond on AXI slave port
      send_axi_response(axi_in_trans, false);
      // Respond on AXI master port
      send_axi_response(axi_out_trans, true);

      // Write payload to FIFO
      stream_fifo_out->write(payload_out);
      SC_LOG_DEBUG(this, "Payload written to FIFO");

      // Flow control
      int link_id = RoutingTable::get_route(
          chip_id, UserSignals::decode(payload_out->user).destination);
      decrement_credits(link_id);
    }

    // Unpacker
    Payload_t *payload = stream_fifo_in->peek();
    if (payload) {
      // Prioritize forwarding packets
      int link_id = RoutingTable::get_route(
          chip_id, UserSignals::decode(payload->user).destination);

      if (link_id != -1) {
        bool is_valid =
            !(credits_out[link_id] == 0 ||
              (credits_out[link_id] == 1 && credits_to_send[link_id] == 0));
        bool is_ready = stream_fifo_out->num_free() > 0;

        if (is_valid && is_ready) {
          stream_fifo_in->read();
          stream_fifo_out->write(payload);
          SC_LOG_DEBUG(this, "Payload written to FIFO");

          // Flow control
          decrement_credits(link_id);
          increment_credits(payload->interconnect_id, payload->credit);
        }
      } else {
        switch (payload->hdr) {
        case TagIdle: {
          stream_fifo_in->read();

          // Flow control
          increment_credits(payload->interconnect_id, payload->credit);
        } break;

        case TagAW:
          if (aw_state == CLEAR) {
            stream_fifo_in->read();
            payload_in = ARM::AXI::Payload::new_payload(
                ARM::AXI::COMMAND_WRITE, payload->axi_ch.addr,
                get_axi_size(axi_width), payload->len, payload->burst);
            payload_in->id = payload->id;
            payload_in->user = payload->user;
            aw_queue.push_back(payload_in);
            axi_out_trans.w_payload = payload_in;

            // Flow control
            increment_credits(payload->interconnect_id, payload->credit);
          }
          break;

        case TagW:
          if (w_state == CLEAR) {
            stream_fifo_in->read();
            payload_in->write_in_beat(payload->axi_ch.data.data());
            w_queue.push_back(payload_in);

            // Flow control
            increment_credits(payload->interconnect_id, payload->credit);
          }
          break;

        case TagAR:
          if (ar_state == CLEAR) {
            stream_fifo_in->read();
            payload_in = ARM::AXI::Payload::new_payload(
                ARM::AXI::COMMAND_READ, payload->axi_ch.addr,
                get_axi_size(axi_width), payload->len, payload->burst);
            payload_in->id = payload->id;
            payload_in->user = payload->user;
            ar_queue.push_back(payload_in);
            axi_out_trans.r_payload = payload_in;

            // Flow control
            increment_credits(payload->interconnect_id, payload->credit);
          }
          break;

        case TagR:
          if (r_state == CLEAR) {
            stream_fifo_in->read();
            ARM::AXI::Payload *r_payload = pending_read_responses.front();
            r_payload->read_in_beat(payload->axi_ch.data.data());
            r_queue.push_back(r_payload);
            if (r_beat_count + 1 == r_payload->get_beat_count())
              pending_read_responses.pop_front();

            // Flow control
            increment_credits(payload->interconnect_id, payload->credit);
          }
          break;

        case TagB:
          if (b_state == CLEAR) {
            stream_fifo_in->read();
            b_queue.push_back(pending_write_responses.front());
            pending_write_responses.pop_front();

            // Flow control
            increment_credits(payload->interconnect_id, payload->credit);
          }
          break;

        default:
          break;
        }
      }
    }

    send_axi_beats();

    // Flow control
    for (size_t i = 0; i < credit_to_send_force.size(); ++i) {
      credit_to_send_force[i] = credits_to_send[i] >= force_send_thresh;
    }

    // Update combinational logic
    update_event.notify(SC_ZERO_TIME);
  }
}

// -------------------------------------------------------
// Committer
// -------------------------------------------------------
void SLNetworkLayer::committer_thread() {
  while (true) {
    wait();

    aw_gnt.write(false);
    w_gnt.write(false);
    b_gnt.write(false);
    ar_gnt.write(false);
    r_gnt.write(false);
    committer_state_d.write(committer_state_q);

    // TODO: Add entropy
    switch (committer_state_q.read()) {
    case Committer::Idle:
      if (is_aw_valid(axi_in_trans.req_phase)) {
        aw_gnt.write(true);
      } else if (is_ar_valid(axi_in_trans.req_phase)) {
        ar_gnt.write(true);
      }

      if (!ar_gnt.read() && !aw_gnt.read() &&
          (is_r_valid(axi_out_trans.req_phase) ||
           is_r_valid_last(axi_out_trans.req_phase))) {
        r_gnt.write(true);
      } else if (!ar_gnt.read() && !aw_gnt.read() &&
                 is_b_valid(axi_out_trans.req_phase)) {
        b_gnt.write(true);
      }

      if (aw_gnt.read() && is_aw_ready(axi_in_trans.rsp_phase)) {
        committer_state_d.write(Committer::AwPend);
      }
      if (ar_gnt.read() && is_ar_ready(axi_in_trans.rsp_phase)) {
        committer_state_d.write(Committer::ArPend);
      }
      break;

    case Committer::AwPend:
      if (is_ar_valid(axi_in_trans.req_phase)) {
        ar_gnt.write(true);
      } else {
        if (is_w_valid(axi_in_trans.req_phase) ||
            is_w_valid_last(axi_in_trans.req_phase)) {
          w_gnt.write(true);
        } else if (is_r_valid(axi_out_trans.req_phase) ||
                   is_r_valid_last(axi_out_trans.req_phase)) {
          r_gnt.write(true);
        } else if (is_b_valid(axi_out_trans.req_phase)) {
          b_gnt.write(true);
        }
      }

      if (is_w_valid_last(axi_in_trans.req_phase) &&
          is_w_ready(axi_in_trans.rsp_phase)) {
        committer_state_d.write((ar_gnt && is_ar_ready(axi_in_trans.req_phase))
                                    ? Committer::ArPend
                                    : Committer::Idle);
      } else {
        committer_state_d.write((ar_gnt && is_ar_ready(axi_in_trans.req_phase))
                                    ? Committer::ArAwPend
                                    : Committer::AwPend);
      }
      break;

    case Committer::ArPend:
      if (is_aw_valid(axi_in_trans.req_phase)) {
        aw_gnt.write(true);
      } else {
        if (is_r_valid(axi_out_trans.req_phase) ||
            is_r_valid_last(axi_out_trans.req_phase)) {
          r_gnt.write(true);
        } else if (is_w_valid(axi_in_trans.req_phase) ||
                   is_w_valid_last(axi_in_trans.req_phase)) {
          w_gnt.write(true);
        } else if (is_b_valid(axi_out_trans.req_phase)) {
          b_gnt.write(true);
        }
      }

      if (is_r_valid_last(axi_in_trans.req_phase) &&
          is_r_ready(axi_in_trans.rsp_phase)) {
        committer_state_d.write((aw_gnt && is_aw_ready(axi_in_trans.req_phase))
                                    ? Committer::AwPend
                                    : Committer::Idle);
      } else {
        committer_state_d.write((aw_gnt && is_aw_ready(axi_in_trans.req_phase))
                                    ? Committer::ArAwPend
                                    : Committer::ArPend);
      }
      break;

    case Committer::ArAwPend: {
      if (is_w_valid(axi_in_trans.req_phase) ||
          is_w_valid_last(axi_in_trans.req_phase)) {
        w_gnt.write(true);
      } else if (is_r_valid(axi_out_trans.req_phase) ||
                 is_r_valid_last(axi_out_trans.req_phase)) {
        r_gnt.write(true);
      } else if (is_b_valid(axi_out_trans.req_phase)) {
        b_gnt.write(true);
      }

      bool aw_pend_idle = is_r_valid_last(axi_in_trans.req_phase) &&
                          is_r_ready(axi_in_trans.rsp_phase);
      bool ar_pend_idle = is_w_valid_last(axi_in_trans.req_phase) &&
                          is_w_ready(axi_in_trans.rsp_phase);

      if (aw_pend_idle & ar_pend_idle) {
        committer_state_d.write(Committer::Idle);
      } else if (aw_pend_idle) {
        committer_state_d.write(Committer::AwPend);
      } else if (ar_pend_idle) {
        committer_state_d.write(Committer::ArPend);
      }
      break;
    }

    default:
      break;
    }
  }
}

// -------------------------------------------------------
// Sender
// -------------------------------------------------------
void SLNetworkLayer::sender_thread() {
  while (true) {
    wait();

    // Prioritize forwarding packets
    Payload_t *payload = stream_fifo_in->peek();
    if (payload) {
      if (UserSignals::decode(payload->user).destination != chip_id) {
        axis_reg_valid_in.write(false);
        axis_reg_ready_in.write(false);
        continue;
      }
    }

    payload_out = new Payload_t(axi_width);

    if (aw_gnt.read()) {
      payload_out->hdr = TagAW;
      payload_out->axi_ch.addr = axi_in_trans.w_payload->get_address();
      payload_out->id = axi_in_trans.w_payload->id;
      payload_out->len = axi_in_trans.w_payload->get_beat_count() - 1;
      payload_out->burst = axi_in_trans.w_payload->get_burst();
      payload_out->user = axi_in_trans.w_payload->user;
    } else if (w_gnt.read()) {
      payload_out->hdr = TagW;
      payload_out->axi_ch.addr = axi_in_trans.w_payload->get_address();
      axi_in_trans.w_payload->write_out_beat(axi_in_trans.w_beat_count,
                                             payload_out->axi_ch.data.data());
      payload_out->id = axi_in_trans.w_payload->id;
      payload_out->len = axi_in_trans.w_payload->get_beat_count() - 1;
      payload_out->burst = axi_in_trans.w_payload->get_burst();
      payload_out->user = axi_in_trans.w_payload->user;
    } else if (ar_gnt.read()) {
      payload_out->hdr = TagAR;
      payload_out->axi_ch.addr = axi_in_trans.r_payload->get_address();
      payload_out->id = axi_in_trans.r_payload->id;
      payload_out->len = axi_in_trans.r_payload->get_beat_count() - 1;
      payload_out->burst = axi_in_trans.r_payload->get_burst();
      payload_out->user = axi_in_trans.r_payload->user;
    } else if (r_gnt.read()) {
      payload_out->hdr = TagR;
      payload_out->axi_ch.addr = axi_out_trans.r_payload->get_address();
      axi_out_trans.r_payload->read_out_beat(axi_out_trans.r_beat_count,
                                             payload_out->axi_ch.data.data());
      payload_out->id = axi_out_trans.r_payload->id;
      payload_out->len = axi_out_trans.r_payload->get_beat_count() - 1;
      payload_out->burst = axi_out_trans.r_payload->get_burst();
      // Source becomes destination
      UserSignals user = UserSignals::decode(axi_out_trans.r_payload->user);
      user.destination = user.source;
      payload_out->user = user.encode();
    } else if (b_gnt.read()) {
      payload_out->hdr = TagB;
      payload_out->id = axi_out_trans.w_payload->id;
      payload_out->len = axi_out_trans.w_payload->get_beat_count() - 1;
      payload_out->burst = axi_out_trans.w_payload->get_burst();
      // Source becomes destination
      UserSignals user = UserSignals::decode(axi_out_trans.w_payload->user);
      user.destination = user.source;
      payload_out->user = user.encode();
    }

    // Credit only packets
    if (payload_out->hdr == TagIdle) {
      UserSignals user;
      user.source = chip_id;
      // Find the interconnect with max credits_to_send
      unsigned index = 0;
      unsigned max_credits = 0;
      for (size_t i = 0; i < credit_to_send_force.size(); ++i) {
        if (credit_to_send_force[i] && credits_to_send[i] > max_credits) {
          index = i;
          max_credits = credits_to_send[i];
        }
      }
      // Find the connected chiplet
      user.destination = RoutingTable::get_destination(chip_id, index);
      payload_out->user = user.encode();
    }

    int link_id = RoutingTable::get_route(
        chip_id, UserSignals::decode(payload_out->user).destination);
    link_id = link_id == -1 ? 0 : link_id;

    payload_out->credit = credits_to_send[link_id];

    bool is_valid =
        ((payload_out->hdr != TagIdle) || credit_to_send_force[link_id]) &&
        !(credits_out[link_id] == 0 ||
          (credits_out[link_id] == 1 && credits_to_send[link_id] == 0));

    axis_reg_valid_in.write(is_valid);
    axis_reg_ready_in.write((stream_fifo_out->num_free() > 0));

    if (axis_reg_valid_in.read() && axis_reg_ready_in.read()) {
      if (aw_gnt.read()) {
        axi_in_trans.rsp_phase = ARM::AXI::AW_READY;
      }
      if (w_gnt.read()) {
        axi_in_trans.rsp_phase = ARM::AXI::W_READY;
      }
      if (b_gnt.read()) {
        axi_out_trans.rsp_phase = ARM::AXI::B_READY;
      }
      if (ar_gnt.read()) {
        axi_in_trans.rsp_phase = ARM::AXI::AR_READY;
      }
      if (r_gnt.read()) {
        axi_out_trans.rsp_phase = ARM::AXI::R_READY;
      }
    } else {
      axi_in_trans.rsp_phase = ARM::AXI::PHASE_UNINITIALIZED;
    }

    axi_in_sig.write(axi_in_trans);
    axi_out_sig.write(axi_out_trans);
  }
}

// -------------------------------------------------------
// Transport functions
// -------------------------------------------------------
tlm_sync_enum SLNetworkLayer::nb_transport_fw(ARM::AXI::Payload &payload,
                                              ARM::AXI::Phase &phase) {
  switch (phase) {
  case ARM::AXI::AW_VALID:
    if (committer_state_d.read() == Committer::AwPend ||
        committer_state_d.read() == Committer::ArAwPend)
      return TLM_ACCEPTED;
    axi_in_trans.w_payload = &payload;
    axi_in_trans.w_beat_count = 0;
    pending_write_responses.push_back(&payload);
    payload.ref();
    break;
  case ARM::AXI::B_READY:
    b_state = b_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  case ARM::AXI::AR_VALID:
    if (committer_state_d.read() == Committer::ArPend ||
        committer_state_d.read() == Committer::ArAwPend)
      return TLM_ACCEPTED;
    axi_in_trans.r_payload = &payload;
    axi_in_trans.r_beat_count = 0;
    pending_read_responses.push_back(&payload);
    payload.ref();
    break;
  default:
    break;
  }

  axi_in_trans.req_phase = phase;

  update_event.notify(SC_ZERO_TIME);
  return TLM_ACCEPTED;
}

tlm_sync_enum SLNetworkLayer::nb_transport_bw(ARM::AXI::Payload &payload,
                                              ARM::AXI::Phase &phase) {
  axi_out_trans.req_phase = phase;

  switch (phase) {
  case ARM::AXI::AR_READY:
    ar_state = ar_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  case ARM::AXI::R_VALID:
    return TLM_ACCEPTED;
  case ARM::AXI::R_VALID_LAST:
    return TLM_ACCEPTED;
  case ARM::AXI::AW_READY:
    aw_state = aw_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  case ARM::AXI::W_READY:
    w_state = w_state == REQ ? ACK : CLEAR;
    return TLM_ACCEPTED;
  case ARM::AXI::B_VALID:
    return TLM_ACCEPTED;
  default:
    SC_REPORT_ERROR(name(), "AXI TLM Protocol: Unrecognized phase");
    return TLM_ACCEPTED;
  }
}

// -------------------------------------------------------
// Helper functions
// -------------------------------------------------------
void SLNetworkLayer::clear_axi_state() {
  if (aw_state == ACK) {
    aw_state = CLEAR;
    aw_queue.pop_front();
  }

  if (w_state == ACK) {
    w_state = CLEAR;
    w_beat_count++;
    if (w_beat_count == w_queue.front()->get_beat_count())
      w_beat_count = 0;
    w_queue.pop_front();
  }

  if (b_state == ACK) {
    b_state = CLEAR;
    b_queue.pop_front();
  }

  if (ar_state == ACK) {
    ar_state = CLEAR;
    ar_queue.pop_front();
  }

  if (r_state == ACK) {
    r_state = CLEAR;
    r_beat_count++;
    if (r_beat_count == r_queue.front()->get_beat_count())
      r_beat_count = 0;
    r_queue.pop_front();
  }
}

void SLNetworkLayer::send_axi_beats() {
  /* Send next payload AWVALID */
  if (aw_state == CLEAR && !aw_queue.empty()) {
    ARM::AXI::Payload *payload = aw_queue.front();
    ARM::AXI::Phase phase = ARM::AXI::AW_VALID;

    aw_state = REQ;
    tlm_sync_enum reply = axi_out.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::AW_READY);
      aw_state = ACK;
    }
  }

  /* Send write beat WVALID */
  if (w_state == CLEAR && !w_queue.empty()) {
    ARM::AXI::Payload *payload = w_queue.front();
    ARM::AXI::Phase phase = (w_beat_count + 1 == payload->get_beat_count())
                                ? ARM::AXI::W_VALID_LAST
                                : ARM::AXI::W_VALID;

    w_state = REQ;
    tlm_sync_enum reply = axi_out.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::W_READY);
      w_state = ACK;
    }
  }

  /* Send write response BVALID */
  if (b_state == CLEAR && !b_queue.empty()) {
    ARM::AXI::Payload *payload = b_queue.front();
    ARM::AXI::Phase phase = ARM::AXI::B_VALID;

    b_state = REQ;
    tlm_sync_enum reply = axi_in.nb_transport_bw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::B_READY);
      b_state = ACK;
    }
  }

  /* Send next payload ARVALID */
  if (ar_state == CLEAR && !ar_queue.empty()) {
    ARM::AXI::Payload *payload = ar_queue.front();
    ARM::AXI::Phase phase = ARM::AXI::AR_VALID;

    ar_state = REQ;
    tlm_sync_enum reply = axi_out.nb_transport_fw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::AR_READY);
      ar_state = ACK;
    }
  }

  /* Send read beat RVALID */
  if (r_state == CLEAR && !r_queue.empty()) {
    ARM::AXI::Payload *payload = r_queue.front();
    ARM::AXI::Phase phase = (r_beat_count + 1 == payload->get_beat_count())
                                ? ARM::AXI::R_VALID_LAST
                                : ARM::AXI::R_VALID;

    r_state = REQ;
    tlm_sync_enum reply = axi_in.nb_transport_bw(*payload, phase);
    if (reply == TLM_UPDATED) {
      sc_assert(phase == ARM::AXI::R_READY);
      r_state = ACK;
    }
  }
}

void SLNetworkLayer::send_axi_response(AxiTrans_t &trans, bool is_master) {
  ARM::AXI::Phase phase = trans.rsp_phase;
  ARM::AXI::Payload *payload = nullptr;

  if (is_aw_ready(phase)) {
    payload = trans.w_payload;
  } else if (is_w_ready(phase)) {
    payload = trans.w_payload;
    trans.w_beat_count =
        (trans.w_beat_count + 1 == trans.w_payload->get_beat_count())
            ? 0
            : trans.w_beat_count + 1;
  } else if (is_master && is_b_ready(phase)) {
    payload = trans.w_payload;
  } else if (is_ar_ready(phase)) {
    payload = trans.r_payload;
  } else if (is_master && is_r_ready(phase)) {
    payload = trans.r_payload;
    trans.r_beat_count =
        (trans.r_beat_count + 1 == trans.r_payload->get_beat_count())
            ? 0
            : trans.r_beat_count + 1;
  }

  if (payload) {
    if (is_master)
      axi_out.nb_transport_fw(*payload, phase);
    else
      axi_in.nb_transport_bw(*payload, phase);
    trans.req_phase = ARM::AXI4::PHASE_UNINITIALIZED;
  }
}

void SLNetworkLayer::increment_credits(int link_id, unsigned credit) {
  credits_out[link_id] += credit;
  credits_to_send[link_id]++;
}

void SLNetworkLayer::decrement_credits(int link_id) {
  credits_out[link_id] -= 1;
  credits_to_send[link_id] = 0;
}