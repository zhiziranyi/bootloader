#ifndef __JUMP_H
#define __JUMP_H

#include <stdint.h>

#define RAM_BASE_ADDR   0x20000000
/* The initial MSP is allowed to point one byte past the final RAM byte. */
#define RAM_TOP_ADDR    0x20020000

void jump_to_app(uint32_t app_addr);

#endif /* __JUMP_H */
