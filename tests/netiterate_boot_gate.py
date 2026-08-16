"""Success boot gate slice for scripts/netboot/net-iterate.sh contract tests."""

from __future__ import annotations

# Success boot gate: scripts/netboot/net-iterate.sh lines 203–295 — the TFTP-verified
# `if printf … "$dns_delta" … kernel8.img` && chain through schedselftest v31–v44,
# runtime v45–v66 banner/ok greps, and the full shell-ready grep. Stale-SD classifier
# blocks reuse many v46–v63 banner strings (~7× each) but are outside this slice.
NET_ITERATE_SUCCESS_BOOT_GATE_START = (
    'if printf \'%s\' "$dns_delta" | grep -qa "$PREFIX/.*kernel8.img"'
)
NET_ITERATE_SUCCESS_BOOT_GATE_SHELL_READY = (
    'grep -qa "shell ready commands=help,protocol,status,heap,queues,tasks,tasks2,'
    'kobjects,drivers,drivercheck,mailboxes,sendtest,supervisor,health,handlecheck,'
    'capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,'
    'sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,'
    'diag,irqs,timers,memcheck,faults,retained,retained-clear,memmap,mmu,pools,'
    'poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,'
    'bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,'
    'heap-double-free-test,panic-test,fault-test,reboot,vmm,asplit,el0,syscall,uaccess,'
    'usermode,process,loader,multiprocess,sdhci,card,block,fat32,mailbox,framebuf,'
    'console,pcie,vl805,xhci"; then'
)


def extract_net_iterate_success_boot_gate(net_iterate: str) -> str:
    start = net_iterate.index(NET_ITERATE_SUCCESS_BOOT_GATE_START)
    shell_ready_idx = net_iterate.index(
        NET_ITERATE_SUCCESS_BOOT_GATE_SHELL_READY, start
    )
    end = shell_ready_idx + len(NET_ITERATE_SUCCESS_BOOT_GATE_SHELL_READY)
    return net_iterate[start:end]
