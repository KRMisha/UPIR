#/usr/bin/env sh

echo Objdump output:

as shellcode.s -o shellcode.o
objdump -d shellcode.o

echo -e '\nHex string:'

objcopy -O binary shellcode.o shellcode.bin
hexdump -v -e '"\\""x" 1/1 "%02x" ""' shellcode.bin

echo
