// sudo sysctl -w vm.mmap_min_addr="0"
// cd shellcode && ./generate.sh && cd ..
// Copy shellcode hex string (excluding address literals) to injectTracepointHandler code[] array
// gcc main.c -o main && ./main
// echo $? # Test that exit code is 77

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#define TRACEPOINT_HANDLER_ADDRESS (void*)0x2000 // TODO: Try to make this work with 0x0000 (not working on Raspberry Pi 3B+ with Raspberry Pi OS)

// Dummy instrumentation function
void foo(void)
{
    printf("void foo\n");
}

// Inject tracepoint handler at a given address which calls the instrumentation code
void injectTracepointHandler(void* tracepointHandlerAddress, void* instrumentationCodeAddress)
{
    //    0:   e59f000c        ldr     r0, [pc, #12]   ; 14 <instrumentation_code_address>
    //    4:   e12fff30        blx     r0
    //    8:   e3a0004d        mov     r0, #77 ; 0x4d
    //    c:   e3a07001        mov     r7, #1
    //   10:   ef000000        svc     0x00000000
    //
    // 00000014 <instrumentation_code_address>:
    //   14:   12345678        .word   0x12345678

    // Hex string of the above instructions, excluding the instrumentation code address which will be appended below
    // Note: the instructions are reversed since they need to be copied in little-endian
    const char code[] = "\x0c\x00\x9f\xe5\x30\xff\x2f\xe1\x4d\x00\xa0\xe3\x01\x70\xa0\xe3\x00\x00\x00\xef";

    // Copy tracepoint handling code to given address
    memcpy(tracepointHandlerAddress, code, sizeof(code) - 1);
    memcpy(tracepointHandlerAddress + sizeof(code) - 1, &instrumentationCodeAddress, sizeof(void*));
    
    // Print tracepoint handler memory contents for debugging
    printf("Tracepoint handler memory contents:");
    const int bufferLength = sizeof(code) - 1 + sizeof(void*);
    for (int i = 0; i < bufferLength; i++)
    {
        if (i % 4 == 0)
        {
            printf(" ");
        }
        printf("%02x", ((char*)tracepointHandlerAddress)[i]);
    }
    printf("\n");
}

int main(void)
{
    printf("int main\n");

    printf("Desired address: %p\n", TRACEPOINT_HANDLER_ADDRESS);

    const long pageSize = sysconf(_SC_PAGESIZE);
    printf("Page size: %li\n", pageSize);

    // Map memory region near address 0 to hold the tracepoint handler code
    uintptr_t* tracepointHandlerAddress = mmap(TRACEPOINT_HANDLER_ADDRESS,
                                               pageSize,
                                               PROT_READ | PROT_WRITE | PROT_EXEC,
                                               MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED,
                                               -1, 0);
    if (tracepointHandlerAddress == MAP_FAILED)
    {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    printf("Obtained address: %p\n", tracepointHandlerAddress);

    // Using mprotect is not necessary since we can set the required permissions when calling mmap
    // if (mprotect(tracepointHandlerAddress, pageSize, PROT_READ | PROT_WRITE | PROT_EXEC))
    // {
    //     perror("mprotect");
    //     exit(EXIT_FAILURE);
    // }

    // Inject tracepoint with foo as the dummy instrumentation function
    void (*fooPtr)() = foo;
    printf("foo's address: %p\n", fooPtr);

    injectTracepointHandler(tracepointHandlerAddress, fooPtr);

    // Simulate tracepoint hit by jumping to tracepoint handler
    asm("mov pc, #0x2000");

    // This code should never be reached
    printf("After tracepoint\n");
}
