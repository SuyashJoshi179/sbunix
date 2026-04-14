#pragma once

// Run kernel self-tests; prints PASS/FAIL for each sub-test to the console.
// Called once from boot(), before sched_init().
void selftest_run(void);
