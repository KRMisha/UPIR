# Uftrace Notes

## Main program flow (start first)

- uftrace.c: `main`
- cmds/record.c: `command_record`
- Setup:
    - `check_binary`
    - `check_perf_event`
- Parent:
    - `do_main_loop`
    - `start_tracing`
    - `writer_thread` (through `pthread_create`)
- Child:
    - `do_child_exec`
    - `setup_child_environ`
        - Checks `opts->patch` and sets the `UFTRACE_PATCH` environment variable to the patch pattern

## Flow from libmcount library to `disasm_check_insns` (start first)

- libmcount/mcount.c
    - `__attribute__((constructor))`
    - `mcount_init`
    - `mcount_startup`
        - Initializes `symtabs`
        - Sets `patch_str` to the value of the `UFTRACE_PATCH` environment variable
        - If `patch_str` is not null (thanks to `-P` option and `setup_child_environ`), `mcount_dynamic_update` gets called with the pattern as its second argument
            - Note: for all functions, this pattern is '.' (for glob *)
- libmcount/dynamic.c
    - **`mcount_dynamic_update`** (also prints stats afterwards)
        - The `symtabs` parameter represents the symbol table
        - The `patch_funcs` parameter represents the patch pattern ('.' for all functions)
    - `do_dynamic_update`
        - Initializes the pattern list (`patterns`) to match functions with `parse_pattern_list`
        - Calls 1. and 2. below
1. Trampoline setup:
    - libmcount/dynamic.c
        - `setup_trampoline`
            - The `mdi` parameter is an out param (`mcount_dynamic_info`). It is populated by the function to later be sent to `patch_func_matched`
    - arch/aarch64/
        - mcount-dynamic.c: `mcount_setup_trampoline`
            - Sets up trampoline in free space after text segment
                - If no text segment is available, mmaps one page later
            - Note: experimentation shows that immediately after the `.text` section is the `.fini` segment, which is 16 bytes long
            - Max jump of 32 MB -> Max address offset of `0x02'00'00'00`
2. Patch functions:
    - libmcount/dynamic.c
        - **`patch_func_matched`**
            - Loops through each `sym` of the current `symtab`. If the `sym` is a local or global function belonging to the pattern list (`match_pattern_list`), the function is patched with `mcount_patch_func`
            - Note: `match_pattern_list` uses the `patterns` global variable, previously initialized by `parse_pattern_list` in `do_dynamic_update`
    - arch/aarch64/
        - mcount-dynamic.c: `mcount_patch_func`
            - This saves the original first instructions (which are to be overwritten by the patching) and replaces them with new instructions to jump to the trampoline
        - mcount-insn.c: `disasm_check_insns`
            - Checks that the function can be patched

## Tracepoint flow from trampoline (start first)

- dynamic.S:
    - `__dentry__`
    - `bl mcount_entry`
- mcount.c:
    - `mcount_entry`
    - `__mcount_entry`
- ... rest of instrumentation code

## ARM-specific files in `arch/aarch64`

- cpuinfo.c
    - Helper function to get CPU model
- dynamic.S
    - Branched to by trampoline when dynamic patching
- mcount-arch.h
    - Only header
    - Declares a few macros for registers
    - Redeclares structs from libmcount/internal.h
    - Declares `disasm_check_insns` from `mcount-insn.c`
- mcount-dynamic.c
    - `- save_orig_code`
    - **`+ mcount_setup_trampoline`**
    - `- get_target_addr`
    - **`+ mcount_patch_func`**
        - Calls `disasm_check_insns`
    - `- revert_normal_func`
    - `+ mcount_arch_dynamic_recover`
- mcount-insn.c
    - `+ mcount_disasm_init`
    - `+ mcount_disasm_finish`
    - `- check_prologue`
    - `- check_body`
    - `- opnd_reg`
    - `- modify_instruction`
    - **`+ disasm_check_insns`**
        - Calls:
            - Capstone: `cs_disasm`
            - `- check_prologue`
            - `- modify_instruction`
                - `- opnd_reg`
            - `- check_body`
- mcount-support.c
    - `- mcount_get_register_arg`
    - `- mcount_get_stack_arg`
    - `+ mcount_arch_get_arg`
    - `+ mcount_arch_get_retval`
    - `+ mcount_arch_plthook_addr`
    - `+ mcount_save_arch_context`
    - `+ mcount_restore_arch_context`
- mcount.S
- plthook.S
