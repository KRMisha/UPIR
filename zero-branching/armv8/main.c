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

#define TRACEPOINT_HANDLER_ADDRESS (void*)0x0000

// Dummy instrumentation function
void foo(void)
{
    printf("void foo\n");
}

// Inject tracepoint handler at a given address which calls the instrumentation code
void injectTracepointHandler(void* tracepointHandlerAddress, void* instrumentationCodeAddress)
{
    //    0:   580000a0        ldr     x0, 14 <instrumentation_code_address>
    //    4:   d63f0000        blr     x0
    //    8:   d28009a0        mov     x0, #0x4d                       // #77
    //    c:   d2800ba8        mov     x8, #0x5d                       // #93
    //   10:   d4000001        svc     #0x0
    //
    // 0000000000000014 <instrumentation_code_address>:
    //   14:   12345678        .word   0x12345678
    //   18:   12345678        .word   0x12345678

    // Hex string of the above instructions, excluding the instrumentation code address which will be appended below
    // Note: the instructions are reversed since they need to be copied in little-endian
    const char code[] = "\xa0\x00\x00\x58\x00\x00\x3f\xd6\xa0\x09\x80\xd2\xa8\x0b\x80\xd2\x01\x00\x00\xd4";

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
    asm("blr xzr");

    // This code should never be reached
    printf("After tracepoint\n");
}
