#ifndef FILE_ASSEMBLER_H_FSYNC
#define FILE_ASSEMBLER_H_FSYNC
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct file_assembler file_assembler_t;

file_assembler_t *file_assembler_open(char const *path, uint64_t size);
void              file_assembler_close(file_assembler_t *);
bool              file_assembler_request_block(file_assembler_t *, uint32_t *);
bool              file_assembler_add_block(file_assembler_t *, uint32_t, uint8_t const *, size_t const);
bool              file_assembler_is_ready(file_assembler_t const *);

#endif
