PROG=TftpSimpleFileSystem
PROG_EFI=$(PROG).efi
SRCS=$(PROG).c
DEPS=$(SRCS:%.c=%.d)
OBJS=$(SRCS:%.c=%.o)

CPPFLAGS= \
	-I../edk2/MdePkg/Include \
	-I../edk2/MdePkg/Include/X64
CFLAGS=-fPIC -g -mno-red-zone -O2
LDFLAGS= \
	-nostdlib \
	-shared \
	-Wl,--dll \
	-Wl,--image-base=0x0 \
	-Wl,-e,_ModuleEntryPoint \
	-Wl,--subsystem,10

all: $(PROG_EFI)
	x86_64-w64-mingw32-strip $^

clean:
	rm -f $(DEPS) $(OBJS) $(PROG_EFI)

$(PROG_EFI): $(OBJS) Makefile
	x86_64-w64-mingw32-gcc $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

%.d: %.c Makefile
	x86_64-w64-mingw32-gcc -M $(CPPFLAGS) -o $@ $<

%.o: %.c Makefile
	x86_64-w64-mingw32-gcc -c $(CPPFLAGS) $(CFLAGS) $< -o $@

ifeq ($(MAKECMDGOALS),obj)
include $(DEPS)
endif
