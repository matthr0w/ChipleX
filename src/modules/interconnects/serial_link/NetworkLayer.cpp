#include "modules/interconnects/serial_link/NetworkLayer.h"

#include "logging.h"

SLNetworkLayer::SLNetworkLayer(sc_module_name name, unsigned chip_id,
                               unsigned axi_width, int num_credits)
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

  SC_THREAD(clk_posedge);
  dont_initialize();
  sensitive << clk.pos();

  SC_THREAD(committer_thread);
  // Sensitive only to read signals
  sensitive << update_event << aw_gnt << ar_gnt << axi_in_sig << axi_out_sig
            << committer_state_q;
  async_reset_signal_is(rst_n, false);

  SC_THREAD(sender_thread);
  // Sensitive only to read signals
  sensitive << update_event << aw_gnt << w_gnt << b_gnt << ar_gnt << r_gnt
            << axis_reg_valid_in << axis_reg_ready_in;
  async_reset_signal_is(rst_n, false);
}

void SLNetworkLayer::clk_posedge() {
  while (true) {
    wait();
    // Log signals
    // SC_LOG_DEBUG(this, "# Internal signals:");
    // SC_LOG_DEBUG(this, "  committer_state_d = " +
    //                        Committer::to_string(committer_state_d.read()));
    // SC_LOG_DEBUG(this,
    //              "  entropy_d         = " +
    //              std::to_string(entropy_d.read()));
    // SC_LOG_DEBUG(this,
    //              "  aw_gnt            = " + std::to_string(aw_gnt.read()));
    // SC_LOG_DEBUG(this, "  w_gnt             = " +
    // std::to_string(w_gnt.read())); SC_LOG_DEBUG(this, "  b_gnt             =
    // " + std::to_string(b_gnt.read())); SC_LOG_DEBUG(this,
    //              "  ar_gnt            = " + std::to_string(ar_gnt.read()));
    // SC_LOG_DEBUG(this, "  r_gnt             = " +
    // std::to_string(r_gnt.read())); SC_LOG_DEBUG(this, "  rsp_phase         =
    // " +
    //                        get_axi_phase_string(axi_in_trans.rsp_phase));
    // SC_LOG_DEBUG(this, "  axis_reg_valid_in = " +
    //                        std::to_string(axis_reg_valid_in.read()));
    // SC_LOG_DEBUG(this, "  axis_reg_ready_in = " +
    //                        std::to_string(axis_reg_ready_in.read()));

    if (aw_state == ACK) {
      aw_state = CLEAR;
      aw_queue.pop_front();
    }

    if (w_state == ACK) {
      w_state = CLEAR;
      w_beat_count++;
      if (w_beat_count == w_queue.front()->get_beat_count()) {
        w_beat_count = 0;
      }
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
      if (r_beat_count == r_queue.front()->get_beat_count()) {
        r_beat_count = 0;
      }
      r_queue.pop_front();
    }

    // Update registers
    committer_state_q.write(committer_state_d.read());
    // TODO: Introduce some randomness and add to committer
    entropy_q.write(entropy_d.read());

    if (axis_reg_valid_in.read() && axis_reg_ready_in.read()) {
      // Respond on AXI slave port
      ARM::AXI::Phase phase = axi_in_trans.rsp_phase;
      ARM::AXI::Payload *axi_payload = nullptr;
      if (is_aw_ready(phase)) {
        axi_payload = axi_in_trans.w_payload;
      } else if (is_w_ready(phase)) {
        axi_payload = axi_in_trans.w_payload;
        if (axi_in_trans.w_beat_count + 1 ==
            axi_in_trans.w_payload->get_beat_count())
          axi_in_trans.w_beat_count = 0;
        else
          axi_in_trans.w_beat_count += 1;
      } else if (is_ar_ready(phase)) {
        axi_payload = axi_in_trans.r_payload;
      }

      if (axi_payload) {
        axi_in.nb_transport_bw(*axi_payload, phase);
        axi_in_trans.req_phase = ARM::AXI4::PHASE_UNINITIALIZED;
      }

      // Respond on AXI master port
      phase = axi_out_trans.rsp_phase;
      axi_payload = nullptr;
      if (is_b_ready(phase)) {
        axi_payload = axi_out_trans.w_payload;
      } else if (is_r_ready(phase)) {
        axi_payload = axi_out_trans.r_payload;
        if (axi_out_trans.r_beat_count + 1 ==
            axi_out_trans.r_payload->get_beat_count())
          axi_out_trans.r_beat_count = 0;
        else
          axi_out_trans.r_beat_count += 1;
      }

      if (axi_payload) {
        axi_out.nb_transport_fw(*axi_payload, phase);
        axi_out_trans.req_phase = ARM::AXI4::PHASE_UNINITIALIZED;
      }

      // Write to fifo
      stream_fifo_out->write(payload_out);
      SC_LOG_DEBUG(this, "Payload written to FIFO");
    }

    // Unpacker
    // Pop the payload from fifo
    Payload_t *payload = stream_fifo_in->peek();
    if (payload) {
      // TODO: Split write response
      switch (payload->hdr) {
      case TagIdle:
        stream_fifo_in->read();
        credit_received = true;
        break;
      case TagAW:
        if (aw_state == CLEAR) {
          payload_in = ARM::AXI::Payload::new_payload(
              ARM::AXI::COMMAND_WRITE, payload->axi_ch.addr,
              get_axi_size(axi_width), payload->len, payload->burst);
          payload_in->id = payload->id;
          payload_in->user = payload->user;
          stream_fifo_in->read();
          aw_queue.push_back(payload_in);
          axi_out_trans.w_payload = payload_in;
          latest_user = payload->user;
          credit_received = true;
        }
        break;
      case TagW:
        if (w_state == CLEAR) {
          payload_in->write_in_beat(payload->axi_ch.data.data());
          stream_fifo_in->read();
          w_queue.push_back(payload_in);
          latest_user = payload->user;
          credit_received = true;
        }
        break;
      case TagAR:
        if (ar_state == CLEAR) {
          payload_in = ARM::AXI::Payload::new_payload(
              ARM::AXI::COMMAND_READ, payload->axi_ch.addr,
              get_axi_size(axi_width), payload->len, payload->burst);
          payload_in->id = payload->id;
          payload_in->user = payload->user;
          stream_fifo_in->read();
          ar_queue.push_back(payload_in);
          axi_out_trans.r_payload = payload_in;
          latest_user = payload->user;
          credit_received = true;
        }
        break;
      case TagR:
        if (r_state == CLEAR) {
          ARM::AXI::Payload *r_payload = pending_read_responses.front();
          r_payload->read_in_beat(payload->axi_ch.data.data());
          stream_fifo_in->read();
          r_queue.push_back(r_payload);
          latest_user = payload->user;
          credit_received = true;
          if (r_beat_count + 1 == r_payload->get_beat_count())
            pending_read_responses.pop_front();
        }
        break;
      default:
        break;
      }

      if (b_state == CLEAR && payload->b_valid) {
        b_queue.push_back(pending_write_responses.front());
        pending_write_responses.pop_front();
      }
    }

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

    // TODO: Fix for inter-chiplet routing
    // Flow control
    if (axis_reg_valid_in.read() && axis_reg_ready_in.read()) {
      credits_out -= 1;
      credits_to_send = 0;
      SC_LOG_DEBUG(this, "credits_out: " << credits_out);
      SC_LOG_DEBUG(this, "credits_to_send: " << credits_to_send);
    }

    if (credit_received) {
      credits_out += payload->credit;
      credits_to_send++;
      SC_LOG_DEBUG(this, "credits_out: " << credits_out);
      SC_LOG_DEBUG(this, "credits_to_send: " << credits_to_send);
    }

    credit_to_send_force = false;
    if (credits_to_send >= force_send_thresh) {
      credit_to_send_force = true;
      SC_LOG_DEBUG(this, "Force sending credits");
    }

    credit_received = false;

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

    switch (committer_state_q.read()) {
    case Committer::Idle:
      if (is_aw_valid(axi_in_trans.req_phase)) {
        aw_gnt.write(true);
      }
      if (is_ar_valid(axi_in_trans.req_phase)) {
        ar_gnt.write(true);
      }

      if (!ar_gnt.read() && !aw_gnt.read() &&
          (is_r_valid(axi_out_trans.req_phase) ||
           is_r_valid_last(axi_out_trans.req_phase))) {
        r_gnt.write(true);
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
        if (is_r_valid(axi_out_trans.req_phase)) {
          r_gnt.write(true);
        }
        if (is_r_valid_last(axi_out_trans.req_phase)) {
          r_gnt.write(true);
        }
        if (is_w_valid(axi_in_trans.req_phase)) {
          w_gnt.write(true);
        }
        if (is_w_valid_last(axi_in_trans.req_phase)) {
          w_gnt.write(true);
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
        if (is_r_valid(axi_out_trans.req_phase)) {
          r_gnt.write(true);
        }
        if (is_r_valid_last(axi_out_trans.req_phase)) {
          r_gnt.write(true);
        }
        if (is_w_valid(axi_in_trans.req_phase)) {
          w_gnt.write(true);
        }
        if (is_w_valid_last(axi_in_trans.req_phase)) {
          w_gnt.write(true);
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
      if (is_r_valid(axi_out_trans.req_phase)) {
        r_gnt.write(true);
      }
      if (is_w_valid(axi_in_trans.req_phase)) {
        w_gnt.write(true);
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

    b_gnt.write(is_b_valid(axi_out_trans.req_phase));
  }
}

// -------------------------------------------------------
// Sender
// -------------------------------------------------------
void SLNetworkLayer::sender_thread() {
  while (true) {
    wait();

    payload_out = new Payload_t(axi_width);
    UserSignals user = UserSignals::decode(latest_user);
    user.destination = user.source;
    user.source = chip_id;
    payload_out->user = user.encode();
    payload_out->credit = credits_to_send;

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
    }

    if (b_gnt.read()) {
      payload_out->b_valid = true;
      payload_out->b = axi_out_trans.w_payload->get_resp();
    }

    axis_reg_valid_in.write((payload_out->hdr != TagIdle) ||
                            payload_out->b_valid || credit_to_send_force);

    if (credits_out == 0) {
      axis_reg_valid_in.write(false);
    } else if (credits_out == 1 && credits_to_send == 0) {
      axis_reg_valid_in.write(false);
    }

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