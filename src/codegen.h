#pragma once
#include "ast.h"

extern int codegen_debug_clif;

int codegen_emit(Program *p, const char *out_path);
