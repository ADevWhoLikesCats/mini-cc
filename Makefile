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

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all clean

