#include "modules/interconnects/serial_link/NetworkLayer.h"

#include "ARM/TLM/arm_axi4.h"
#include "logging.h"

SLNetworkLayer::SLNetworkLayer(sc_module_name name, unsigned axi_width,
                               int num_credits)
    : sc_module(name), axi_width(axi_width), num_credits(num_credits),
      force_send_thresh(num_credits - 4),
      axi_in("axi_in", *this, &SLNetworkLayer::nb_transport_fw,
             ARM::TLM::PROTOCOL_AXI4, axi_width) {
  // Initial values
  committer_state_q.write(Committer::Idle);
  entropy_q.write(false);
  axis_reg_valid_in.write(false);
  axis_reg_ready_in.write(false);
  axis_reg_valid_out.write(false);
  axis_reg_ready_out.write(false);
  credits_out_q.write(num_credits);
  credits_to_send_q.write(0);
  credit_to_send_force.write(false);

  SC_METHOD(clk_posedge);
  dont_initialize();
  sensitive << clk.pos();

  SC_THREAD(committer_thread);
  // Sensitive only to read signals
  sensitive << axi_in_event << aw_gnt << ar_gnt << axi_in_sig << axi_out_sig
            << committer_state_q;
  async_reset_signal_is(rst_n, false);

  SC_THREAD(sender_thread);
  // Sensitive only to read signals
  sensitive << axi_in_event << aw_gnt << w_gnt << b_gnt << ar_gnt << r_gnt
            << axis_reg_valid_in << axis_reg_ready_in;
  async_reset_signal_is(rst_n, false);

  SC_THREAD(flow_control_thread);
  async_reset_signal_is(rst_n, false);
}

void SLNetworkLayer::clk_posedge() {
  // Log signals
  if (comb_logic_updated) {
    SC_LOG_DEBUG(this, "# Internal signals:");
    SC_LOG_DEBUG(this, "  committer_state_d = " +
                           Committer::to_string(committer_state_d.read()));
    SC_LOG_DEBUG(this,
                 "  entropy_d         = " + std::to_string(entropy_d.read()));
    SC_LOG_DEBUG(this,
                 "  aw_gnt            = " + std::to_string(aw_gnt.read()));
    SC_LOG_DEBUG(this, "  w_gnt             = " + std::to_string(w_gnt.read()));
    SC_LOG_DEBUG(this, "  b_gnt             = " + std::to_string(b_gnt.read()));
    SC_LOG_DEBUG(this,
                 "  ar_gnt            = " + std::to_string(ar_gnt.read()));
    SC_LOG_DEBUG(this, "  r_gnt             = " + std::to_string(r_gnt.read()));
    SC_LOG_DEBUG(this, "  rsp_phase         = " +
                           get_axi_phase_string(axi_in_trans.rsp_phase));
  }

  // Update registers
  committer_state_q.write(committer_state_d);
  // TODO: Introduce some randomness
  entropy_q.write(entropy_d);
  // credits_out_q.write(credits_out_d);
  credits_to_send_q.write(credits_to_send_d);

  // Respond on AXI slave port and write to FIFO
  if (comb_logic_updated && axis_reg_valid_in.read() &&
      axis_reg_ready_in.read()) {
    ARM::AXI::Phase phase = axi_in_trans.rsp_phase;
    ARM::AXI::Payload *axi_payload = nullptr;
    if (is_aw_ready(phase)) {
      axi_payload = axi_in_trans.w_payload;
    } else if (is_w_ready(phase)) {
      axi_payload = axi_in_trans.w_payload;
      axi_in_trans.w_beat_count += 1;
    }
    axi_in.nb_transport_bw(*axi_payload, phase);
    stream_fifo_out.write(payload_out);
    SC_LOG_DEBUG(this, "Payload written to FIFO");
  }

  comb_logic_updated = false;
}

// -------------------------------------------------------
// Committer
// -------------------------------------------------------
void SLNetworkLayer::committer_thread() {
  while (true) {
    wait();

    comb_logic_updated = true;

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
        // SC_LOG_DEBUG(this, "Committer: Granting AW");
      }
      if (is_ar_valid(axi_in_trans.req_phase)) {
        ar_gnt.write(true);
        // SC_LOG_DEBUG(this, "Committer: Granting AR");
      }

      if (!ar_gnt.read() && !aw_gnt.read() &&
          is_r_valid(axi_out_trans.rsp_phase)) {
        r_gnt.write(true);
        // SC_LOG_DEBUG(this, "Committer: Granting R");
      }

      if (aw_gnt.read() && is_aw_ready(axi_in_trans.rsp_phase)) {
        committer_state_d.write(Committer::AwPend);
        // SC_LOG_DEBUG(this, "Committer: Transition: Idle -> AwPend");
      }
      if (ar_gnt.read() && is_ar_ready(axi_in_trans.rsp_phase)) {
        committer_state_d.write(Committer::ArPend);
        // SC_LOG_DEBUG(this, "Committer: Transition: Idle -> ArPend");
      }
      break;

    case Committer::AwPend:
      if (is_ar_valid(axi_in_trans.req_phase)) {
        ar_gnt.write(true);
        // SC_LOG_DEBUG(this, "Committer: Granting AR (while AW pending)");
      } else {
        if (is_r_valid(axi_out_trans.rsp_phase)) {
          r_gnt.write(true);
          // SC_LOG_DEBUG(this, "Committer: Granting R");
        }
        if (is_w_valid(axi_in_trans.req_phase)) {
          w_gnt.write(true);
          // SC_LOG_DEBUG(this, "Committer: Granting W");
        }
        if (is_w_valid_last(axi_in_trans.req_phase)) {
          w_gnt.write(true);
          // SC_LOG_DEBUG(this, "Committer: Granting W");
        }
      }

      if (is_w_valid_last(axi_in_trans.req_phase) &&
          is_w_ready(axi_in_trans.rsp_phase)) {
        committer_state_d.write((ar_gnt && is_ar_ready(axi_in_trans.req_phase))
                                    ? Committer::ArPend
                                    : Committer::Idle);
        // SC_LOG_DEBUG(this,
        //"Committer: AW burst done, transitioning to "
        //<< ((ar_gnt && is_ar_ready(axi_in_trans.req_phase))
        //? "ArPend"
        //: "Idle"));
      } else {
        committer_state_d.write((ar_gnt && is_ar_ready(axi_in_trans.req_phase))
                                    ? Committer::ArAwPend
                                    : Committer::AwPend);
      }
      break;

    case Committer::ArPend:
      if (is_aw_valid(axi_in_trans.req_phase)) {
        aw_gnt.write(true);
        // SC_LOG_DEBUG(this, "Committer: Granting AW (while AR pending)");
      } else {
        if (is_r_valid(axi_out_trans.rsp_phase)) {
          r_gnt.write(true);
          // SC_LOG_DEBUG(this, "Committer: Granting R");
        }
        if (is_w_valid(axi_in_trans.req_phase)) {
          w_gnt.write(true);
          // SC_LOG_DEBUG(this, "Committer: Granting W");
        }
      }

      if (is_r_valid_last(axi_in_trans.req_phase) &&
          is_r_ready(axi_in_trans.rsp_phase)) {
        committer_state_d.write((aw_gnt && is_aw_ready(axi_in_trans.req_phase))
                                    ? Committer::AwPend
                                    : Committer::Idle);
        // SC_LOG_DEBUG(this,
        //"Committer: AR burst done, transitioning to "
        //<< ((aw_gnt && is_aw_ready(axi_in_trans.req_phase))
        //? "AwPend"
        //: "Idle"));
      } else {
        committer_state_d.write((aw_gnt && is_aw_ready(axi_in_trans.req_phase))
                                    ? Committer::ArAwPend
                                    : Committer::ArPend);
      }
      break;

    case Committer::ArAwPend: {
      if (is_r_valid(axi_out_trans.req_phase)) {
        r_gnt.write(true);
        // SC_LOG_DEBUG(this, "Committer: Granting R");
      }
      if (is_w_valid(axi_in_trans.req_phase)) {
        w_gnt.write(true);
        // SC_LOG_DEBUG(this, "Committer: Granting W");
      }

      bool aw_pend_idle = is_r_valid_last(axi_in_trans.req_phase) &&
                          is_r_ready(axi_in_trans.rsp_phase);
      bool ar_pend_idle = is_w_valid_last(axi_in_trans.req_phase) &&
                          is_w_ready(axi_in_trans.rsp_phase);

      if (aw_pend_idle & ar_pend_idle) {
        committer_state_d.write(Committer::Idle);
        // SC_LOG_DEBUG(this, "Committer: Both AR/AW pending done, back to
        // Idle");
      } else if (aw_pend_idle) {
        committer_state_d.write(Committer::AwPend);
        // SC_LOG_DEBUG(this, "Committer: AW pending done, stay in AwPend");
      } else if (ar_pend_idle) {
        committer_state_d.write(Committer::ArPend);
        // SC_LOG_DEBUG(this, "Committer: AR pending done, stay in ArPend");
      }
      break;
    }

    default:
      break;
    }

    b_gnt.write(is_b_valid(axi_out_trans.req_phase));
    if (is_b_valid(axi_out_trans.req_phase)) {
      // SC_LOG_DEBUG(this, "Committer: Granting B");
    }
  }
}

// -------------------------------------------------------
// Sender
// -------------------------------------------------------
void SLNetworkLayer::sender_thread() {
  while (true) {
    wait();

    comb_logic_updated = true;

    payload_out = new Payload_t(axi_width);
    payload_out->credit = credits_to_send_q;

    if (aw_gnt.read()) {
      payload_out->axi_ch.addr = axi_in_trans.w_payload->get_address();
      payload_out->user = axi_in_trans.w_payload->user;
      payload_out->hdr = TagAW;
      // SC_LOG_DEBUG(this, "Sender: Sending AW, addr=" << std::hex
      //                                                <<
      //                                                payload_out->axi_ch.addr
      //                                                << std::dec);
    } else if (w_gnt.read()) {
      axi_in_trans.w_payload->write_out_beat(axi_in_trans.w_beat_count,
                                             payload_out->axi_ch.data.data());
      payload_out->user = axi_in_trans.w_payload->user;
      payload_out->hdr = TagW;
      // SC_LOG_DEBUG(this,
      //              "Sender: Sending W beat #" << axi_in_trans.w_beat_count);
    } else if (ar_gnt.read()) {
      payload_out->axi_ch.addr = axi_in_trans.r_payload->get_address();
      payload_out->user = axi_in_trans.r_payload->user;
      payload_out->hdr = TagAR;
      // SC_LOG_DEBUG(this, "Sender: Sending AR, addr=" << std::hex
      //                                                <<
      //                                                payload_out->axi_ch.addr
      //                                                << std::dec);
    } else if (r_gnt.read()) {
      axi_out_trans.r_payload->read_out_beat(axi_out_trans.r_beat_count,
                                             payload_out->axi_ch.data.data());
      payload_out->user = axi_in_trans.r_payload->user;
      payload_out->hdr = TagR;
      // SC_LOG_DEBUG(this,
      //              "Sender: Sending R beat #" << axi_out_trans.r_beat_count);
    }

    if (b_gnt.read()) {
      payload_out->b_valid = true;
      payload_out->b = axi_out_trans.w_payload->get_resp();
      // SC_LOG_DEBUG(this, "Sender: Sending B response, resp=" <<
      // payload_out->b);
    }

    axis_reg_valid_in.write((payload_out->hdr != TagIdle) ||
                            payload_out->b_valid ||
                            credit_to_send_force.read());

    if (credits_out_q.read() == 0) {
      axis_reg_valid_in.write(false);
      // SC_LOG_DEBUG(this, "Sender: Blocked: credits_out_q==0");
    } else if (credits_out_q.read() == 1 && credits_to_send_q.read() == 0) {
      axis_reg_valid_in.write(false);
      // SC_LOG_DEBUG(this,
      //              "Sender: Blocked: credits_out_q==1, no credits to
      //              return");
    }

    axis_reg_ready_in.write((stream_fifo_out.num_free() > 0));
    // SC_LOG_DEBUG(this, "Sender: FIFO free=" << stream_fifo_out.num_free());

    if (axis_reg_valid_in.read() && axis_reg_ready_in.read()) {
      if (aw_gnt.read()) {
        axi_in_trans.rsp_phase = ARM::AXI::AW_READY;
        // SC_LOG_DEBUG(this, "Sender: AW_READY");
      }
      if (w_gnt.read()) {
        axi_in_trans.rsp_phase = ARM::AXI::W_READY;
        // SC_LOG_DEBUG(this, "Sender: W_READY");
      }
      if (b_gnt.read()) {
        // SC_LOG_DEBUG(this, "Sender: B_READY (stubbed)");
      }
      if (ar_gnt.read()) {
        axi_in_trans.rsp_phase = ARM::AXI::AR_READY;
        // SC_LOG_DEBUG(this, "Sender: AR_READY");
      }
      if (r_gnt.read()) {
        // SC_LOG_DEBUG(this, "Sender: R_READY (stubbed)");
      }
    } else {
      axi_in_trans.rsp_phase = ARM::AXI::PHASE_UNINITIALIZED;
      // SC_LOG_DEBUG(this, "Sender: NONE");
    }

    axi_in_sig.write(axi_in_trans);
    axi_out_sig.write(axi_out_trans);
  }
}

// -------------------------------------------------------
// Flow control
// -------------------------------------------------------
void SLNetworkLayer::flow_control_thread() {
  while (true) {
    wait();

    SC_LOG_DEBUG(this, "Flow control tick");

    credits_out_d.write(credits_out_q);
    credits_to_send_d.write(credits_to_send_q);
    credit_to_send_force.write(false);

    SC_LOG_DEBUG(this, "Initial state: credits_out_q="
                           << credits_out_q.read()
                           << " credits_to_send_q=" << credits_to_send_q.read()
                           << " force_send_thresh=" << force_send_thresh);

    // Send empty packets with credits if there are too many
    // credits to send but no AXI request transaction
    if (credits_to_send_q >= force_send_thresh) {
      credit_to_send_force.write(true);
      SC_LOG_DEBUG(this, "Force sending credits (credits_to_send_q="
                             << credits_to_send_q.read() << ")");
    }

    if (axis_reg_valid_in && axis_reg_ready_in) {
      unsigned old_credits = credits_out_d.read();
      credits_out_d.write(old_credits - 1);
      credits_to_send_d.write(0);

      SC_LOG_DEBUG(
          this,
          "AXIS transaction accepted: axis_reg_valid_in=1 axis_reg_ready_in=1");
      SC_LOG_DEBUG(this, "Credits decremented: " << old_credits << " -> "
                                                 << credits_out_d.read());
      SC_LOG_DEBUG(this, "Credits to send reset to 0");
    }

    // TODO: Increment credits with incoming payloads
    SC_LOG_DEBUG(this, "End of cycle: credits_out_d="
                           << credits_out_d.read()
                           << " credits_to_send_d=" << credits_to_send_d.read()
                           << " credit_to_send_force="
                           << credit_to_send_force.read());
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
    payload.ref();
    break;
  case ARM::AXI::AR_VALID:
    if (committer_state_d.read() == Committer::ArPend ||
        committer_state_d.read() == Committer::ArAwPend)
      return TLM_ACCEPTED;
    axi_in_trans.r_payload = &payload;
    payload.ref();
    break;
  default:
    break;
  }

  axi_in_trans.req_phase = phase;

  axi_in_event.notify(SC_ZERO_TIME);
  return TLM_ACCEPTED;
}