#pragma once
#include "ast.h"

Program *clang_bridge_parse(const char *path);
void     clang_bridge_shutdown(void);
