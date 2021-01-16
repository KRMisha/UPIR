@ call instrumentation code
ldr r0, instrumentation_code_address
blx r0

@ exit
mov r0, #77         @ status = 77
mov r7, #1          @ exit is syscall #1
svc #0              @ invoke syscall
@ TODO: Replace exit with return to caller (arbitrary address which will be overwritten) instead

@ instrumentation code address (arbitrary address which will be overwritten)
instrumentation_code_address: .word 0x12345678
