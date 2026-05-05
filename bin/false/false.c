/*
 * false — return 1 (failure), no output. POSIX standard utility.
 *
 * Counterpart to true(1); used to force a failure status, e.g.
 * `cmd || false` to invert success/failure semantics.
 */
int main(void) { return 1; }
