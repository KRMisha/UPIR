# Branch-to-Zero Tracepoint Tests

This investigates whether a tracepoint based on branch-to-zero (or near zero) instructions would work on ARMv7 and ARMv8. This is simulated by dynamically patching the program instructions to add shellcode (acting as the tracepoint handler) located near the zero virtual memory address, which is generally unmapped by default. A solution based on this branch-to-zero technique would then replace any instruction with the single `asm(...)` instruction which branches to the handler.

The `armv7` directory contains the solution for ARMv7, based on a direct manipulation of the `pc` register with a `mov` instruction. The `armv8` contains the solution for ARMv8, which instead uses an indirect branch with the zero register (`xzr`), since direct manipulation of the `pc` register is disallowed in ARMv8.

## Goal

By placing the tracepoint handling code near the zero address in memory, it is possible to branch to the tracepoint handling code with an absolute address (rather than a relative branch). Using an absolute address (either through `pc` manipulation with an immediate value or with the zero register on ARMv8) avoids the 32 MB limitation instrinsic to relative branching on ARM, allowing branching to tracepoint handlers in binaries with a text segment larger than 64 MB (see [`nop-analysis/README.md`](../nop-analysis/README.md)). Normally, absolute branching requires two instructions: loading the address in a register and then doing an indirect branch. The branch-to-zero solution instead offers a way to branch to an absolute address with a single instruction by avoiding the register loading instruction, allowing for simpler tracepoints which can be patched atomically.

## Implementation

### ARMv7

In ARMv7 assembly, it is possible to use `pc` as the destination register for `mov` instructions. By using an immediate, whose value can be in the range 0-65535, a single `mov` instruction can act as an absolute branch to an address from 0-65535.

The following links provide additional information on using this technique to branch to an absolute address:
- The answers hint to the possibility of using `pc` as the operand for instructions such as `ldr`: https://stackoverflow.com/questions/32304646/arm-assembly-branch-to-address-inside-register-or-memory
- Keil's ARM user guide for the `mov` instruction shows that `pc` can be used as the destination register: https://www.keil.com/support/man/docs/armasm/armasm_dom1361289878994.htm

The tracepoint is a single instruction and looks as follows:
```arm
mov pc, <absolute offset from 0> // Where the offset can be in the range 0-65535
```

Since this branch instruction is in no way a call, the link register `lr` is not updated with the return address of the calling code. This means that a common tracepoint handler (one common address to branch to in the range 0-65535) would not be able to distinguish which tracepoint was hit, making it near useless and incapable of returning to the original code. To avoid this problem, the strategy for ARMv7 is for each tracepoint to have its own branch destination (its own tracepoint handler). This way, each tracepoint handler could be injected with a different return address to the instrumented code, since the link register would not be useful to know where to return once the tracepoint handling is complete.

This means that the full range of 0-65535 should be used for tracepoint handlers. Each handler needs 6 instructions:
- 2 for the absolute branch to the instrumentation code (`ldr` + `blx`)
- 2 for the absolute branch to return to the original code after the tracepoint once the instrumentation is processed (`ldr + bx`)
- 2 `.word` instructions to contain the addresses for these two jumps

See the `armv7/shellcode/shellcode.s` file for reference. With 6 instructions/handler, a maximum of 65536/6 = 10922 tracepoints could coexist at a time.

### ARMv8

In ARMv8 assembly (specifically AArch64), unlike ARMv7, the `pc` register cannot be explicitly used in instructions. This means that using `mov` to load an immediate into `pc` to perform an absolute branch to an address near zero is not possible. Fortunately, ARMv8 exposes the zero register, `xzr`, whose value is equal to zero for certain instructions, including `blr`. This register is not available on ARMv7.

The list of links below provides additional information on the zero register and branching on ARMv8:
- Limitations regarding the use of the `pc` register in ARMv8: https://developer.arm.com/documentation/dui0801/a/Overview-of-AArch64-state/Program-Counter-in-AArch64-state
- List of ARMv8 registers: https://developer.arm.com/documentation/dui0801/b/Overview-of-AArch64-state/Registers-in-AArch64-state
- Use of the `adr` instruction if the `pc` cannot be used explicitly: https://stackoverflow.com/questions/41906688/what-are-the-semantics-of-adrp-and-adrl-instructions-in-arm-assembly
- Absolute branching in ARMv8 requiring two instructions: https://stackoverflow.com/questions/44949124/absolute-jump-with-a-pc-relative-data-source-aarch64

Using the zero register on ARMv8, it is possible to use an indirect branch instruction to jump to a tracepoint handler located at the zero address:
```arm
blr xzr
```

Unlike the ARMv7 solution, this instruction is a proper call, which stores the return address of the calling code in the `lr` register. This means that a common handler can be used for all tracepoints, which is essential for this to work since there is no way to jump to different handlers using the `xzr` register; the sole handler must be located at address zero. The tracepoint handler can then check the value of `lr` to determine if the source instruction is a valid tracepoint (rather than a null pointer dereference) by checking if the value in `lr` belongs to the addresses of traced instructions. This lookup can be used to obtain the original instruction (overwritten by the tracepoint), execute it out of line, and finally return to the calling code by using the `ret lr` instruction.

It is important to note that since the `blr xzr` instruction is call which modifies the `lr` register, the previous value of `lr` is overwritten. This may not initially seem like an issue, since the prologue of a function should take care of pushing the `fp` and `lr` registers on the stack to allow for other functions to be called from its body (see [this link](https://azeria-labs.com/functions-and-the-stack-part-7/) for a refresher). Before the function returns with `ret lr`, the `fp` and `lr` registers are popped off the stack, restoring them to their original value, untouched by any calls.

However, this is indeed a problem if the instruction is part of a **leaf** function, i.e. a function which does not make any calls as part of its body. In that case, compiler optimizations will entirely omit the function prologue, the `lr` register will not be pushed to the stack, and the epilogue of the function will assume `lr` is unchanged when returning, rather than restoring it from the stack. Dynamically inserting a call (for a tracepoint handler) in the body of a leaf function will overwrite the link register. Because the compiler optimizations assumed no calls would be made in the function body, this will make the function return to the wrong address, since `lr` will not be restored from the stack.

This means that the `blr xzr` tracepoint technique works when the instruction to trace is part of the body of a **non-leaf** function, since the previous value of `lr` is saved to the stack in the function prologue and restored from the stack in the epilogue. But if the instruction is part of a non-inlined leaf function, this technique should not be used.

## Tests and results

To test the dynamic injection of a tracepoint handler near zero, a sample program which patches its zero address at runtime and performs a branch-to-zero was written for each architecture. Detailed explanations for its inner workings can be found in the `armv7/main.c` and `armv8/main.c` files, but its general operation is as follows:

1. `mmap` a page near zero which will contain the tracepoint handler. Since the mapped memory's permissions can be set upon mapping, no `mprotect` is needed later on.
2. Inject the code for the tracepoint handler at this mapped memory. This code is generated from the `shellcode/shellcode.s` file by using the `generate.sh` script for each architecture.
    - This injected code represents the tracepoint handler and would normally call the instrumentation code responsible for more actions (such as logging). It would then execute the original tracepoint instructions and branch back to the code just after the tracepoint.
    - In the case of this test, the shellcode simply prints to the console and exits with a status code of 77 to indicate the branch-to-zero was successful.
    - Useful reference for writing ARM shellcode: https://azeria-labs.com/writing-arm-shellcode/
3. Branch to the tracepoint handler with inline assembly.
    - For ARMv7, the instruction is `mov pc, <address of handler>`.
    - For ARMv8, the instruction is `blr xzr`.

To modify or run it locally (for the appropriate architecture), simply follow the comments at the top of each `main.c` source file.

Thee execution of both the ARMv7 and ARMv8 solutions shows that the technique works properly. The ARMv7 solution was tested on a Raspberry Pi 3B+ running Raspberry Pi OS, whose build is for armv7l (checked with `uname -m`), which is 32-bit. The ARMv8 test was peerformed with an ARMv8 AArch64 Ubuntu Server 18.04 VM with QEMU. Both tests work regardless of the compilation optimization level (`-O3`).

## Limitations

The main limitation with this approach is the need to bypass a default kernel protection for mapping low virtual addresses with `mmap`. The `mmap_min_addr` value, found in `/proc/sys/vm/mmap_min_addr`, specifies the minimum virtual address that can be mmap'ed. Most distributions have a default configuration of at least 4096, which disallows direct access to address 0, needed for the ARMv8 solution at the very least. To fix this, one of the following commands needs to be used prior to starting the tracing process:
```sh
echo 0 | sudo tee /proc/sys/vm/mmap_min_addr
# Or
sudo sysctl -w vm.mmap_min_addr="0"
```

This can pose problem since root access is necessary to temporarily reduce the minimum mmap address beforehand. Furthermore, reducing this value removes an additional safeguard against potential kernel null dereference exploits. The following links are a useful reference regarding `mmap_min_addr`.
- Debian reference on `mmap_min_addr`: https://wiki.debian.org/mmap_min_addr
- Kernel null dereference exploits and `mmap_min_addr`: https://blogs.oracle.com/linux/much-ado-about-null%3a-exploiting-a-kernel-null-dereference-v2

As mentioned above, another limitation is the maximum number of simultaneous tracepoints on ARMv7, limited by how many tracepoint handlers can fit in the range 0-65535. Finally, the unsuitability of the ARMv8 solution for leaf functions can limit the number of traceable instructions, but more tests are required to assess how often this poses problem in practice.

## Possible enhancements

While the advantage of this technique is that it allows tracepoints to replace only a single instruction and to not be bound by any maximum text segment size constraints, it is not applicable in all cases. It could be enhanced with other techniques to cover the situations it cannot handle.

For ARMv7, for instance, the limitation regarding the maximum number of simultaneous tracepoints could be mitigated by using relative branching (with its maximum range of 32 MB - see [`nop-analysis/README.md`](../nop-analysis/README.md)) for instructions closer to the beginning or end of a long text segment. The branch-to-zero could then be used mainly for instructions whose position near the middle of the text segment would otherwise make relative branching to a tracepoint handler difficult.

For ARMv8, using two instructions - one to push `fp` and `lr` on the stack and one for the `blr xzr` - could be a way to add tracepoints to non-inlined optimized leaf functions which do not push their frame registers on the stack. If single-instruction tracepoints are preferred, falling back to 32 MB relative branch instructions could also work if the text segment size is not too large or if the instruction is near the beginning or end of the text segment.
