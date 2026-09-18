# ─────────────────────────────────────────────────────────────
# mini-cc — Makefile for MSYS2 / MinGW-w64
# ─────────────────────────────────────────────────────────────

CC       := gcc
CFLAGS   := -std=c11 -Wall -Wextra -O0 -g
ROOT     := /c/Users/hrish/Downloads/mini-cc
LLVM     := /d/LLVM-full/LLVM
CLIFT    := /d/Cranelift/craneliftc

INCLUDES := -I$(ROOT)/headers -I$(ROOT)/src -I$(LLVM)/include
LDFLAGS  := -L$(LLVM)/lib -L$(CLIFT)/target/release
LDLIBS   := -lclang -lcraneliftc -lws2_32 -luserenv -lbcrypt -lntdll

SRCS := src/main.c src/ast.c src/clang_bridge.c src/sema.c src/codegen.c
OBJS := $(SRCS:.c=.o)

BIN := bin/mini-cc.exe

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
	
rung1: bin/mini-cc.exe
	./bin/mini-cc.exe --emit-obj test/rung1.c
	mv out.o rung1.o
	gcc test/rung1_main.c rung1.o -o bin/rung1.exe
	./bin/rung1.exe

.PHONY: rung1

rung2: $(BIN) rung2.o rung2_main.o
	./bin/ld.exe -m i386pep -Bdynamic -o bin/rung2.exe bin/crt2.o bin/crtbegin.o rung2_main.o rung2.o -Lbin/lib -lmingw32 -lgcc -lgcc_eh -lmoldname -lmingwex -lmsvcrt -ladvapi32 -lshell32 -luser32 -lkernel32 bin/crtend.o
	./bin/rung2.exe

rung2.o: test/rung2.c $(BIN)
	$(BIN) --emit-obj $<
	mv out.o $@

rung2_main.o: test/rung2_main.c
	$(CC) -std=c11 -c $< -o $@

.PHONY: rung2

rung3: $(BIN) rung3.o rung3_main.o
	./bin/ld.exe -m i386pep -Bdynamic -o bin/rung3.exe bin/crt2.o bin/crtbegin.o rung3_main.o rung3.o -Lbin/lib -lmingw32 -lgcc -lgcc_eh -lmoldname -lmingwex -lmsvcrt -ladvapi32 -lshell32 -luser32 -lkernel32 bin/crtend.o
	./bin/rung3.exe

rung3.o: test/rung3.c $(BIN)
	$(BIN) --emit-obj $<
	mv out.o $@

rung3_main.o: test/rung3_main.c
	$(CC) -std=c11 -c $< -o $@

.PHONY: rung3

rung456: $(BIN) rung456.o rung456_main.o
	./bin/ld.exe -m i386pep -Bdynamic -o bin/rung456.exe bin/crt2.o bin/crtbegin.o rung456_main.o rung456.o -Lbin/lib -lmingw32 -lgcc -lgcc_eh -lmoldname -lmingwex -lmsvcrt -ladvapi32 -lshell32 -luser32 -lkernel32 bin/crtend.o
	./bin/rung456.exe

rung456.o: test/rung456.c $(BIN)
	$(BIN) --emit-obj $<
	mv out.o $@

rung456_main.o: test/rung456_main.c
	$(CC) -std=c11 -c $< -o $@

.PHONY: rung456

rung8: $(BIN) rung8.o rung8_main.o
	./bin/ld.exe -m i386pep -Bdynamic -o bin/rung8.exe bin/crt2.o bin/crtbegin.o rung8_main.o rung8.o -Lbin/lib -lmingw32 -lgcc -lgcc_eh -lmoldname -lmingwex -lmsvcrt -ladvapi32 -lshell32 -luser32 -lkernel32 bin/crtend.o
	./bin/rung8.exe

rung8.o: test/rung8.c $(BIN)
	$(BIN) --emit-obj $<
	mv out.o $@

rung8_main.o: test/rung8_main.c
	$(CC) -std=c11 -c $< -o $@

.PHONY: rung8

rung9: $(BIN) rung9.o rung9_main.o
	./bin/ld.exe -m i386pep -Bdynamic -o bin/rung9.exe bin/crt2.o bin/crtbegin.o rung9_main.o rung9.o -Lbin/lib -lmingw32 -lgcc -lgcc_eh -lmoldname -lmingwex -lmsvcrt -ladvapi32 -lshell32 -luser32 -lkernel32 bin/crtend.o
	./bin/rung9.exe

rung9.o: test/rung9.c $(BIN)
	$(BIN) --emit-obj $<
	mv out.o $@

rung9_main.o: test/rung9_main.c
	$(CC) -std=c11 -c $< -o $@

.PHONY: rung9	

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all clean

