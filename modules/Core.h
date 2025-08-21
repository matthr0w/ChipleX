#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "common/AXIUtils.h"
#include "common/Tracker.h"
#include "common/protocol/ChipletPayload.h"

#include "include/logging.h"

using namespace sc_core;
using namespace tlm;
using namespace tlm_utils;

SC_MODULE(Core) {
public:
  // -------------------------------------------------------
  // trackers
  // -------------------------------------------------------
  UtilizationTracker utilization_tracker;

  // -------------------------------------------------------
  // sockets
  // -------------------------------------------------------
  simple_initiator_socket<Core> isocket;
  simple_target_socket<Core> irq_socket;

  Core(sc_module_name name, AXIUtils & axi_utils, unsigned int chip_id,
       unsigned int core_id, sc_time irq_delay);

  std::function<void(Core &, UtilizationTracker *)> thread_fn;
  std::function<void(Core &, UtilizationTracker *, tlm_generic_payload *)>
      interrupt_fn;

  void core_thread();
  void interrupt_thread();

private:
  AXIUtils & axi_utils;

  struct RequestHandle {
    ChipletPayload *transaction;
    sc_event done;
    bool ready = false;

    RequestHandle() : transaction(nullptr) {}
    ~RequestHandle() { delete transaction; }

    void notify(sc_time delay) {
      ready = true;
      done.notify(delay);
    }

    void wait() {
      if (!ready) {
        ::wait(done);
      }
    }

    ChipletPayload *get_payload() const { return transaction; }
  };

  std::unordered_map<tlm::tlm_generic_payload *, RequestHandle *>
      request_handles;

  sc_mutex request_mutex;

  std::deque<tlm_generic_payload *> irq_queue;

  // -------------------------------------------------------
  // parameters
  // -------------------------------------------------------
  const unsigned int chip_id;
  const unsigned int core_id;
  const sc_time irq_delay;

  // -------------------------------------------------------
  // events
  // -------------------------------------------------------
  sc_event req_evt;
  sc_event irq_req_evt;

  // -------------------------------------------------------
  // transport functions
  // -------------------------------------------------------
  tlm_sync_enum nb_transport_fw_irq(tlm_generic_payload & transaction,
                                    tlm_phase & phase, sc_time & delay);

  tlm_sync_enum nb_transport_bw(tlm_generic_payload & transaction,
                                tlm_phase & phase, sc_time & delay);

  // -------------------------------------------------------
  // delay model
  // -------------------------------------------------------
  struct DelayModel {
  private:
    const Core &module;

  public:
    DelayModel(const Core &m) : module(m) {}

    sc_time irq_transfer(tlm_generic_payload &transaction) const {
      SC_LOG_DELAY(&module, transaction, "IRQ Transfer", module.irq_delay);
      return module.irq_delay;
    }
  };

  DelayModel delays{*this};

  // -------------------------------------------------------
  // AXI methods
  // -------------------------------------------------------
private:
  RequestHandle *read_internal(int request_id, int destination_id,
                               uint32_t address, bool fixed_address,
                               unsigned char *data, unsigned int data_length,
                               unsigned int burst_type, bool is_volatile);

  RequestHandle *write_internal(int request_id, int destination_id,
                                uint32_t address, bool fixed_address,
                                unsigned char *data, unsigned int data_length,
                                unsigned int burst_type, bool is_volatile);

public:
  RequestHandle *read(int request_id, int destination_id, uint32_t address,
                      unsigned char *data, unsigned int data_length,
                      bool is_volatile);

  RequestHandle *read_fixed(int request_id, int destination_id,
                            uint32_t address, unsigned char *data,
                            unsigned int data_length, bool is_volatile);

  RequestHandle *read_wrap(int request_id, int destination_id, uint32_t address,
                           unsigned char *data, unsigned int data_length,
                           bool is_volatile);

  RequestHandle *write(int request_id, int destination_id, uint32_t address,
                       unsigned char *data, unsigned int data_length,
                       bool is_volatile);

  RequestHandle *write(int request_id, int destination_id, unsigned char *data,
                       unsigned int data_length);

  RequestHandle *write_fixed(int request_id, int destination_id,
                             uint32_t address, unsigned char *data,
                             unsigned int data_length, bool is_volatile);

  RequestHandle *write_fixed(int request_id, int destination_id,
                             unsigned char *data, unsigned int data_length);

  RequestHandle *write_wrap(int request_id, int destination_id,
                            uint32_t address, unsigned char *data,
                            unsigned int data_length, bool is_volatile);

  RequestHandle *write_wrap(int request_id, int destination_id,
                            unsigned char *data, unsigned int data_length);
};