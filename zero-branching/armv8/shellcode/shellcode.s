// call instrumentation code
ldr x0, instrumentation_code_address
blr x0

// exit
mov x0, #77         // status = 77
mov x8, #93         // exit is syscall #93
svc #0              // invoke syscall
// TODO: Replace exit with return to caller (using `lr`) instead

// instrumentation code address (arbitrary address which will be overwritten)
instrumentation_code_address: .dword 0x1234567812345678
