import argparse

import m5
from m5.objects import (AddrRange, Cache, Process, Root, SEWorkload,
                        SimpleMemory, SrcClockDomain, System, SystemXBar,
                        VoltageDomain)


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmd", required=True, help="workload ELF to run")
    parser.add_argument("--options", default="", help="workload argv string")
    parser.add_argument("--cpu-type", default="RiscvMinorCPU",
                        help="gem5 CPU SimObject class (carries the ISA prefix)")
    parser.add_argument("--mem-mode", default="timing",
                        help="memory mode: 'timing' for pipelined CPUs, "
                             "'atomic' for an AtomicSimpleCPU")
    parser.add_argument("--clock", default="1GHz")
    parser.add_argument("--l1i-size", default="32kB")
    parser.add_argument("--l1d-size", default="32kB")
    parser.add_argument("--l1i-assoc", type=int, default=2)
    parser.add_argument("--l1d-assoc", type=int, default=2)
    parser.add_argument("--cacheline", type=int, default=64)
    parser.add_argument("--mem-latency", default="30ns")
    parser.add_argument("--mem-size", default="512MiB")
    return parser.parse_args()


def l1_cache(size, assoc):
    return Cache(size=size, assoc=assoc, tag_latency=2, data_latency=2,
                 response_latency=2, mshrs=4, tgts_per_mshr=20)


def main():
    args = parse_args()

    system = System()
    system.clk_domain = SrcClockDomain(clock=args.clock,
                                       voltage_domain=VoltageDomain())
    system.mem_mode = args.mem_mode
    system.mem_ranges = [AddrRange(args.mem_size)]
    system.cache_line_size = args.cacheline

    cpu_cls = getattr(m5.objects, args.cpu_type)
    system.cpu = cpu_cls()

    system.membus = SystemXBar()

    system.cpu.icache = l1_cache(args.l1i_size, args.l1i_assoc)
    system.cpu.dcache = l1_cache(args.l1d_size, args.l1d_assoc)
    system.cpu.icache.cpu_side = system.cpu.icache_port
    system.cpu.dcache.cpu_side = system.cpu.dcache_port
    system.cpu.icache.mem_side = system.membus.cpu_side_ports
    system.cpu.dcache.mem_side = system.membus.cpu_side_ports

    system.cpu.createInterruptController()
    system.system_port = system.membus.cpu_side_ports

    system.mem_ctrl = SimpleMemory(range=system.mem_ranges[0],
                                   latency=args.mem_latency)
    system.mem_ctrl.port = system.membus.mem_side_ports

    system.workload = SEWorkload.init_compatible(args.cmd)
    process = Process()
    process.cmd = [args.cmd] + (args.options.split() if args.options else [])
    system.cpu.workload = process
    system.cpu.createThreads()

    root = Root(full_system=False, system=system)
    m5.instantiate()
    exit_event = m5.simulate()
    print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")


main()
