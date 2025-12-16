import argparse
from typing import List
from dataclasses import dataclass
from sdfgen import SystemDescription, Sddf, DeviceTree
from importlib.metadata import version

assert version("sdfgen").split(".")[1] == "26", "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain
VirtualMachine = SystemDescription.VirtualMachine
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map

@dataclass
class Board:
    name: str
    arch: SystemDescription.Arch
    paddr_top: int
    serial: str
    timer: str
    block: str
    partition: int


BOARDS: List[Board] = [
    Board(
        name="qemu_virt_aarch64",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x6_0000_000,
        serial="pl011@9000000",
        timer="timer",
        block="virtio_mmio@a003e00",
        partition=0,
    )
]

def generate(sdf_file: str, output_dir: str, dtb: DeviceTree):
        
    guest_memory = MemoryRegion(sdf, name="guest_ram", size=0x10000000)

    # Create VMM
    vmm = ProtectionDomain("VMM", "vmm.elf", priority=1, stack_size=0x4000)
    vmm_map = Map(guest_memory, vaddr=0x30000000, perms="rw") #, setvar_vaddr="guest_ram_vaddr", setvar_size="guest_ram_size")
    vmm.add_map(vmm_map)

    # Create guest machine
    guest = VirtualMachine("solo5", [VirtualMachine.Vcpu(id=0)])
    guest_map = Map(guest_memory, vaddr=0x0, perms="rwx")
    guest.add_map(guest_map)
    vmm.set_virtual_machine(guest)
    
    # Add our components to system description
    sdf.add_mr(guest_memory)    
    sdf.add_pd(vmm)
    
    # Setup timer
    timer_driver = ProtectionDomain("timer_driver", "timer_driver.elf", priority=201)
    sdf.add_pd(timer_driver)
    
    timer_node = dtb.node(board.timer)
    assert timer_node is not None   
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)
    timer_system.add_client(vmm)    
    
    # Setup serial
    serial_driver = ProtectionDomain("serial_driver", "serial_driver.elf", priority=200)
    serial_virt_tx = ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=199, stack_size=0x2000)
    serial_virt_rx = ProtectionDomain("serial_virt_rx", "serial_virt_rx.elf", priority=199, stack_size=0x2000)
    sdf.add_pd(serial_driver)
    sdf.add_pd(serial_virt_tx)
    sdf.add_pd(serial_virt_rx)
    
    serial_node = dtb.node(board.serial)
    assert serial_node is not None
    serial_system = Sddf.Serial(sdf, serial_node, serial_driver, serial_virt_tx, virt_rx=serial_virt_rx)
    serial_system.add_client(vmm)
    
    # Setup block
    block_driver = ProtectionDomain("blk_driver", "blk_driver.elf", priority=200, stack_size=0x2000)
    block_virt = ProtectionDomain("blk_virt", "blk_virt.elf", priority=199, stack_size=0x2000)    
    sdf.add_pd(block_driver)
    sdf.add_pd(block_virt)    
    
    block_node = dtb.node(board.block)
    assert block_node is not None
    block_system = Sddf.Blk(sdf, block_node, block_driver, block_virt)
    partition = int(args.partition) if args.partition else board.partition
    block_system.add_client(vmm, partition=partition)       
    timer_system.add_client(block_driver)  
    
    # Serialise driver configs    
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)
    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert block_system.connect()
    assert block_system.serialise_config(output_dir)
    
    # Output final system description
    with open(f"{output_dir}/{sdf_file}", "w+") as f:
        f.write(sdf.render())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=True)
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--board", required=True, choices=[b.name for b in BOARDS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)
    parser.add_argument("--partition", required=False)

    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb)
