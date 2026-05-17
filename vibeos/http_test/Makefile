CC = riscv64-unknown-elf-gcc
OBJCOPY = riscv64-unknown-elf-objcopy
TCC = /root/trade_new/os/mini-riscv-os/tcc-riscv32/riscv32-tcc
CFLAGS = -nostdlib -ffreestanding -fno-builtin -fno-pic -fno-pie -mcmodel=medany -march=rv32imac -mabi=ilp32 -O2 -Wall -Wextra
LDFLAGS = -T app.ld -nostdlib
TCCFLAGS = -nostdlib -I.

all: test.bin hello.elf fault.bin writefault.bin jumpfault.bin selfmem.bin
tcc: hello_tcc.elf

hello.elf: crt0.c hello.c app.h app.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ crt0.c hello.c

crt0_tcc.o: crt0.c app.h
	$(CC) $(CFLAGS) -c -o $@ crt0.c

hello_tcc.o: hello.c app.h
	$(TCC) $(TCCFLAGS) -c -o $@ hello.c

app_sys_tcc.o: app_sys.S
	$(CC) $(CFLAGS) -c -o $@ app_sys.S

hello_tcc.elf: crt0_tcc.o hello_tcc.o app_sys_tcc.o app.ld
	$(CC) -nostdlib -march=rv32imac -mabi=ilp32 -T app.ld -o $@ crt0_tcc.o hello_tcc.o app_sys_tcc.o

test.bin: hello.elf
	cp $< $@

fault.elf: crt0.c fault.c app.h app.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ crt0.c fault.c

fault.bin: fault.elf
	$(OBJCOPY) -O binary $< $@

writefault.elf: crt0.c writefault.c app.h app.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ crt0.c writefault.c

writefault.bin: writefault.elf
	$(OBJCOPY) -O binary $< $@

jumpfault.elf: crt0.c jumpfault.c app.h app.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ crt0.c jumpfault.c

jumpfault.bin: jumpfault.elf
	$(OBJCOPY) -O binary $< $@

selfmem.elf: crt0.c selfmem.c app.h app.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ crt0.c selfmem.c

selfmem.bin: selfmem.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f hello.elf test.bin fault.elf fault.bin writefault.elf writefault.bin jumpfault.elf jumpfault.bin selfmem.elf selfmem.bin hello_tcc.elf crt0_tcc.o hello_tcc.o app_sys_tcc.o
