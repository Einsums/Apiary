// Parsed by the package test with -nostdlibinc: <stddef.h> has to come from
// the Clang builtin headers apiary's libclang looks for, or size_t does not
// resolve.
#include <stddef.h>

/// The size of a probe.
size_t probe_size();
