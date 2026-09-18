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

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all clean

