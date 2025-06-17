# High Level Simulation

This project provides a high-level simulation environment using SystemC.

## SystemC Installation

### Prerequisites

**Fedora**

```bash
sudo dnf install cmake clang
sudo dnf install glibc-static libstdc++-static  # required for static linking
```

### Installation Steps

1. **Clone the SystemC repository**  
   Clone the official SystemC repository from Accellera:

   ```bash
   git clone https://github.com/accellera-official/systemc.git
   ```

2. **Create a build directory**  
   Change into the SystemC directory and create a build subdirectory:

   ```bash
   cd systemc
   mkdir build
   cd build
   ```

3. **Generate the Makefiles using CMake**  
   Configure the build to install SystemC into a local directory (`../install`) and build using static libraries:

   ```bash
   cmake .. -DCMAKE_INSTALL_PREFIX=../install -DBUILD_SHARED_LIBS=OFF
   ```

4. **Compile the source**  
   Compile the project using all available CPU cores:

   ```bash
   make -j$(nproc)
   ```

5. **Install SystemC**  
   Install the compiled library files:

   ```bash
   make install
   ```

### Environment Variables

`~/.bashrc`

```bash
export SYSTEMC_PATH=/path/to/systemc/install
export LD_LIBRARY_PATH=$SYSTEMC_PATH/lib:$LD_LIBRARY_PATH
```


```bash
source ~/.bashrc
```

## Building

```bash
make
```

## Model Information

### Overview

![Overview.png](/docs/Overview.png)

The figure above illustrates the basic structure of the system. It consists of a configurable number of chiplets connected in a ring topology (blue arrows) and an FPGA. When only two chiplets are present, they are linked by a single bidirectional connection. The FPGA module is similar to the chiplet modules, and its connections to the chiplets (green arrows) can be configured via the command-line interface (see [Usage](#usage)).

**Chiplet**

![Chiplet.png](/docs/Chiplet.png)

The figure above shows the structure of a single chiplet module. The arrows represent initiator sockets. Each chiplet consists of two cores, a bus, RAM with its memory controller, and a combined interconnect, which includes an interconnect protocol module that communicates with the various interconnect modules.

Each core has a main thread and an interrupt handler, the latter of which is triggered when the core receives an IRQ from the interconnect protocol module (see [Message Sequence Diagrams](#message-sequence-diagrams)). Both functions are programmable by the user (see [User Code](#user-code)).

The bus uses a simple arbitration scheme where each request is queued and processed sequentially. Requests from the interconnect are prioritized to reduce the risk of deadlock.

The memory controller is responsible for translating incoming transactions to the appropriate RAM addresses. It also supports a flag in the payload extension that enables it to automatically assign a free address for write requests (see [Chiplet Extension](#chiplet-extension)).

The interconnect structure – comprising the protocol layer and the various interconnect modules – can be parameterized via configuration files to model different types of interconnects (see [Configurations](#configurations)). The main role of the protocol layer is to route incoming requests to the appropriate interconnect module and to split the data stream into flits. Each interconnect module maintains its own Tx and Rx buffers, where flits can reside during an ongoing transaction.

Interconnect0 connects to the FPGA, Interconnect1 to Chiplet *n–1*, and Interconnect2 to Chiplet *n+1*.

**FPGA**

![FPGA.png](/docs/FPGA.png)

The figure above shows the structure of the FPGA module. Its operation is very similar to that of the chiplet modules, with a few minor differences:

- Instead of two cores, the FPGA contains a single data generator, which can also be programmed by the user (see [User Code](#user-code)).

- It includes one interconnect for each chiplet: Interconnect0 connects to Chiplet1, Interconnect1 to Chiplet2, and so on.

### Usage

The program provides a command-line interface that allows the user to configure the basic simulation setup. More advanced parameters can be modified in the configuration files (see [Configurations](#configurations)).

```bash
./sim [options]
Options:
   --time=<ns>               Set simulation time in nanoseconds (default: unlimited)
   --chiplets=<n>            Set number of chiplets (minimum: 2, default: 2)
   --connection-type=<type>  Set interconnect type: Custom, PCIe, UCIe, SPI (default: Custom)
   --connections=1,2,3       Set FPGA connection targets: 1,2,...,n (default: 1,2)
   --chiplet-distance=<um>   Set distance between chiplets in micrometers (default: 100)
   --fpga-distance=<mm>      Set distance between FPGA and chiplets in millimeters (default: 5000)
   --ber=<prob>              Set bit error rate (default: 1e-12)
   --logging=level           Set logging level: INFO, WARN, ERROR, DEBUG, SILENT (default: ERROR)
   --help                    Show this help message
```

### Configurations

The model is highly configurable. All configuration files are located in the `configs` directory. The `Chiplet.yaml` and `FPGA.yaml` files contain basic parameters for the respective modules, while the `interconnects` subdirectory includes various configuration files for different interconnect types. These interconnect types can be selected via the command-line interface (see [Usage](#usage)).

### User Code

The `usercode` directory contains the `UserCode.h` file, which allows the user to program the cores and the FPGA data generator. Detailed instructions on how to do this are provided within the file itself. Examples can be found in the `examples` subdirectory.

### Message Sequence Diagrams

The following subsections present message sequence diagrams for the various operations. Note that the diagrams are simplified for clarity. In the actual implementation, the model follows the TLM 2.0 standard, using both request and response phases.

**On-Chip Write Transactions**

![OnWriteTransaction.png](/docs/msds/OnWriteTransaction.png)

The diagram shows two different write operations issued by a core to its on-chip RAM. In the second operation, the memory controller selects a free address (e.g., 0x2000) because the `fixed_address` flag in the payload extension is not set (see [Chiplet Extension](#chiplet-extension)).

**On-Chip Read Transaction**

![OnReadTransaction.png](/docs/msds/OnReadTransaction.png)

The diagram shows a read operation issued by a core to its on-chip RAM.

**Off-Chip Write Transaction**

![OffWriteTransaction.png](/docs/msds/OffWriteTransaction.png)

The diagram shows a write operation issued by a core on Chiplet *n* to the RAM of Chiplet *n+1*. The interconnect protocol layer of Chiplet *n* splits the data stream into flits and places them into the appropriate Tx buffer. The core can resume processing once the last flit has been buffered. After the write completes, the interconnect protocol on Chiplet *n+1* sends an IRQ to Core0 of Chiplet *n+1* (IRQs for write operations are always sent to Core0).

**Off-Chip Read Transaction**

![OffReadTransaction.png](/docs/msds/OffReadTransaction.png)

The diagram shows a read operation issued by a core on Chiplet *n* to the RAM of Chiplet *n+1*. As with the off-chip write operation, the core resumes processing after the final data flit is buffered. Once the data is read on the remote chiplet, it is sent back to the source chiplet and written into its RAM. Afterward, the protocol layer sends an IRQ to the issuing core with the address where the read data was stored.

### Chiplet Extension

To control both on-chip and off-chip traffic, several flags are required for each transaction:

- **`request_id`**: Set by the core; used to identify the request later.

- **`core_id`**: Set by the bus; used by the protocol layer to send the IRQ to the correct core.

- **`source_id`**: Set by the protocol layer; used to identify the source chiplet or FPGA for the response.

- **`destination_id`**: Set by the core; used to route the transaction to the correct chiplet or FPGA.

- **`fixed_address`**: Set by the core; instructs the memory controller whether to allocate a free address or use the one provided.

- **`is_volatile`**: Set by the core; instructs the cache controller to bypass the cache and always load from RAM.

The following extensions are only required when the transaction is split into data flits and transmitted over the interconnect:

- **`flit_count`**: Specifies how many flits the data is divided into.

- **`flit_id`**: Identifies the position of the flit within the data stream.

- **`flit_padding`**: Indicates the number of zero-padded bytes in the flit.

Additionally, the `start_time` variable is used to measure transaction latency, while the `success` flag indicates whether a transmission was successful. Both are intended solely for internal evaluation. They do not represent real-world header fields and should not be relied upon by user logic.

## RISC-V Cycle Estimation

### Prerequisites

**RISC-V GNU Compiler Toolchain** 

https://github.com/riscv-collab/riscv-gnu-toolchain

**RISC-V Proxy Kernel and Boot Loader**

https://github.com/riscv-software-src/riscv-pk

**Spike RISC-V ISA Simulator**

https://github.com/riscv-software-src/riscv-isa-sim

### Simulation Steps

1. **Modify the user code to measure clock cycles**  
   Insert code to read the cycle CSR via RDCYCLE before and after the workload to capture cycle counts:

   ```C
   #include <stdio.h>

   uint64_t read_cycles(void) {
      uint64_t cycles;
      asm volatile ("rdcycle %0" : "=r" (cycles));
      return cycles;
   }

   int main() {
      uint64_t start_cycles = read_cycles();

      // USERCODE //

      uint64_t end_cycles = read_cycles();

      printf("Cycles: %lu\n", end_cycles - start_cycles);

      return 0;
   }
   ```

2. **Compile the code using the RISC-V toolchain**

   ```bash
   riscv64-unknown-elf-gcc -o usercode usercode.c
   ```

3. **Run the compiled program on Spike simulator**

   ```bash
   spike pk usercode
   ```