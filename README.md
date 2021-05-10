# Efficient Dynamic Instrumentation with Branch-Based Tracepoints for ARM

The purpose of this research is to find ways to efficiently and dynamically add tracepoints for instrumenting programs which are already running. The techniques should create tracepoints which do not severely affect performance when hit, whose insertion is thread-safe, and which can cover the widest possible range of cases (using many techniques if necessary). Since conventional trap-based tracepoints incur a heavy overhead because of context switching, the focus here is on using branch instructions to branch to tracepoint handlers (which, in turn, call the instrumentation code).

## ARM-specific information

### Branch instructions

The maximum range for a relative branch on ARM is 32 MB, but this can be done in a single instruction, which is great for tracepoints. The 32 MB maximum range limits where a tracepoint handler (the destination for the tracepoint branch instruction) can be placed, however.

An absolute branch can normally only be achieved with an indirect jump (using a register). This means that the address to jump to must first be loaded into the register (with a `mov` or `ldr` instruction) before the branch can be performed (with a `bx` instruction), for a total of two instructions. Two instructions can be trickier to work with as tracepoints because the two instructions cannot be guaranteed to be modified atomically.

Reference resources:
- Overview of ARM branch instructions: https://community.arm.com/developer/ip-products/processors/b/processors-ip-blog/posts/branch-and-call-sequences-explained
- Branch ranges: https://developer.arm.com/documentation/dui0489/e/arm-and-thumb-instructions/branch-and-control-instructions/b--bl--bx--blx--and-bxj
- ARM assembly tutorial: https://azeria-labs.com/writing-arm-assembly-part-1/
- ARMv7 absolute branch: https://stackoverflow.com/questions/32304646/arm-assembly-branch-to-address-inside-register-or-memory
- ARMv8 absolute branch: https://stackoverflow.com/questions/44949124/absolute-jump-with-a-pc-relative-data-source-aarch64

### Cache coherence

After replacing any instruction in the running process, the ICache needs to be flushed to be in sync with the memory. This can be achieved with the `__builtin___clear_cache()` function in GCC, which takes care of calling `__clear_cache()`.

Reference resources:
- Caches and self-modifying code: https://community.arm.com/developer/ip-products/processors/b/processors-ip-blog/posts/caches-and-self-modifying-code?
- ICache and DCache coherence: https://developer.arm.com/documentation/ddi0151/c/caches--write-buffer--and-physical-address-tag--pa-tag--ram/cache-coherence
- Question on patching code at runtime for ARMv7: https://reverseengineering.stackexchange.com/questions/3728/patching-arm7-code-during-runtime
- Kernel ARM instruction patching: https://lwn.net/Articles/620640/

### Cross-thread considerations

Useful resources for thread-safe code modification:
- Very detailed answer on potential techniques: https://stackoverflow.com/questions/39295261/how-to-synchronize-on-arm-when-one-thread-is-writing-code-which-the-other-thread
- Multi-core cache coherency: https://developer.arm.com/documentation/den0024/a/Multi-core-processors/Multi-core-cache-coherency-within-a-cluster

## Survey of existing solutions

### General overview of tracing systems

- Linux tracing systems and how they fit together: https://jvns.ca/blog/2017/07/05/linux-tracing-systems/
- ARM dynamic tracing tools: https://elinux.org/images/3/32/ELC_2017_NA_dynamic_tracing_tools_on_arm_aarch64_platform.pdf
- Linux tracer choice flowchart: http://www.brendangregg.com/blog/2015-07-08/choosing-a-linux-tracer.html
- Paper analyzing the available kernel and userspace tracers on Linux: https://www.researchgate.net/publication/323709567_Survey_and_Analysis_of_Kernel_and_Userspace_Tracers_on_Linux_Design_Implementation_and_Overhead
- eBPF and bpftrace: https://www.joyfulbikeshedding.com/blog/2019-01-31-full-system-dynamic-tracing-on-linux-using-ebpf-and-bpftrace.html
- Uprobe: http://www.brendangregg.com/blog/2015-06-28/linux-ftrace-uprobe.html
- Some useful comparisons with existing work (section 7): https://www.eecg.utoronto.ca/~ashvin/publications/dbt-asplos2012.pdf

### Kprobes and optimized kprobes (optprobes)

Since kprobes runs in kernel space, it does not directly address the goal of having a userspace tracer. Nevertheless, its tracing techniques, especially its optimized branch-based probes, can be used as a reference for implementing a similar solution in userspace.

Useful links:
- How kprobes works: https://vjordan.info/log/fpga/how-linux-kprobes-works.html
    - Important limitation to note regarding jprobes on ARM:
        > Note that in some architectures (e.g.: arm64 and sparc64) the stack
          copy is not done, as the actual location of stacked parameters may be
          outside of a reasonable MAX_STACK_SIZE value and because that location
          cannot be determined by the jprobes code. In this case the jprobes
          user must be careful to make certain the calling signature of the
          function does not cause parameters to be passed on the stack (e.g.:
          more than eight function arguments, an argument of more than sixteen
          bytes, or more than 64 bytes of argument data, depending on
          architecture).
- Kprobes for ARM: https://elinux.org/images/b/b6/Kprobes_for_ARM-ELC2007.pdf

Optprobes are implemented and functional for 32-bit ARM (ARMv7 or ARMv8 AArch32). They are achieved by using a single relative branch instruction as a tracepoint which jumps to a trampoline. The current implementation recognizes the 32 MB maximum branch range as a current limitation of the single-instruction probe. It has been discussed (but not yet implemented) to use 2-instruction tracepoints to cover cases where this limitation is an issue thanks to an absolute branch, but this would add additional complexity since two instructions would need to be patched safely. Patch notes:
- https://lore.kernel.org/patchwork/patch/491242/
- https://lwn.net/Articles/624517/
- https://lwn.net/Articles/625714/

For 64-bit ARM (ARMv8 AArch64), unfortunately, the optprobes support is not yet ready:
- https://www.infradead.org/~mchehab/rst_features/feature_arm64.html
- https://mjmwired.net/kernel/Documentation/features/debug/optprobes
- http://lists.infradead.org/pipermail/linux-arm-kernel/2015-June/349211.html

### Uftrace

While uftrace can patch an executable which was not compiled with `-pg` options, it cannot yet trace a process which is already running. What uftrace calls "dynamic tracing" only refers to the fact that programs need not be recompiled to be analyzed by uftrace. They must still be started from uftrace as subprocesses. It's important to note that this "dynamic tracing" is only implemented for ARMv8 (64-bit); the ARMv7 (32-bit) version is not yet available.

Since uftrace is modelled after ftrace, it is specifically designed to trace functions. When an executable is compiled with `-pg`, a call to `mcount` is inserted at the beginning of every function. Uftrace overrides the traced program's `libmcount` with its own thanks to `LD_PRELOAD`, thus modifying what code gets executed when `mcount` is called. But if the executable was not compiled with profiling options, no such call to `mcount` exists, and it must be patched in after the fact; this is what uftrace calls "dynamic tracing" or "dynamic patching".

Dynamic patching is enabled with the `-P` option. For dynamic patching, uftrace first parses the list of available functions from the executable's ELF data and compares it with the desired patch filters provided as command-line options. A common trampoline is then setup in unused space just after the text segment. For ARMv8 (AArch64), uftrace systematically replaces the first two instructions of the functions which should be traced. The first instruction pushes the `fp` and `lr` register on the stack (`stp`) to avoid losing their value in the call. The second instruction branches to the address of the common trampoline using a relative branch (`bl`) (the 32 MB limitation thus still applies). Only once all this patching is done is the traced process finally started.

When a patched function is executed, the control branches to the trampoline. This trampoline uses an indirect branch instruction to jump to `__dentry__`, which itself eventually calls `mcount_entry` (rather than `mcount`).

In a nutshell, this means uftrace for ARM is also restricted by the 32 MB limitation for relative jumps and uses two instructions, which brings complications when adding or removing tracepoints while the process is running.

A detailed breakdown of uftrace's program flow and the functions involved can be found in the [`uftrace-notes.md`](uftrace-notes.md) file. This can be a useful reference to get going quickly with uftrace modifications to improve its dynamic tracing capabilities.

## Experiments

### NOP instruction frequency and text segment sizes

See the [`nop-analysis` README](nop-analysis/README.md).

### Single-instruction branch-to-zero tracepoints

See the [`zero-branching` README](zero-branching/README.md).

### Tracing any instruction with uftrace

Since uftrace is specifically designed to trace functions, tracing arbitrary instructions would require an extensive redesign of the architecture. Setting aside the implications for the record/replay format and the function-centric user experience (statistics, filtering, etc), the main redesign which would need to be done would be regarding the use of `symtabs` to decide which functions to trace. Currently, the `symtabs` struct contains a list of high-level symbols (`syms`) extracted from the ELF, but whose type is restricted to elements such as functions or ELF data (see the `symtype` struct). Thus, to instrument any function, the core logic of uftrace would need to be moved away from an ELF-based filtering of functions to using a disassembler such as Capstone from the very beginning. This would have far-reaching impacts on uftrace's architecture.

### Dynamically adding tracepoints to a running program

Currently, uftrace only supports patching functions before the process is started. However, using Clément Guidi's uftrace client/daemon feature, it is possible to send instructions to uftrace from a client to dynamically change options after the traced process has been started. Using this client/daemon feature, Gabriel Pollo-Guilbert's refactor to dynamically add tracepoints to a running process was ported and tested on ARMv8. The code can be found here: https://github.com/KRMisha/uftrace/tree/armv8-runtime-patching. Note that the traced process is still started with uftrace for now - it is not attached to while it is running.

In local tests, dynamically adding a tracepoint to a process with multiple running threads could be done successfully and consistently. However, these successful tests do not necessarily prove that the solution would work in all cases. In theory, the fact that two instructions must be patched by uftrace when adding a tracepoint means that there is a risk for a thread to execute a half-patched tracepoint, causing errors.

One way to resolve this issue would be to employ the same technique used on x86 to patch variable-length instructions: a trap instruction is first placed to catch any code executing the instructions while they are being patched, and it is replaced with the first instruction once all the instructions after it have been patched. Enhancing uftrace to use single-instruction tracepoints rather than its current 2-instruction tracepoints would also prevent the issue from occuring in the first place. This could potentially be achieved with a technique like kprobes' optprobes (by removing the need to push registers on the stack directly in the tracepoint) or with branch-to-zero tracepoints.
