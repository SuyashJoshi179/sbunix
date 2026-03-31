#include <stdint.h>

void trap_init(void);
void trap_handler(uint64_t scause, uint64_t sepc, uint64_t stval);
