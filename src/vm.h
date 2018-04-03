#ifndef UFOLD_VM_H
#define UFOLD_VM_H

#include <stddef.h>
#include <stdint.h>
#include "stdbool.h"

//\ Virtual State Machine
typedef struct ufold_vm_struct ufold_vm_t;

//\ VM Options
typedef struct ufold_vm_options_struct ufold_vm_options_t;

//\ Writer for Output
typedef bool (*ufold_vm_write_t)(const void* ptr, const size_t size);

//\ Memory Reallocator
typedef void* (*ufold_vm_realloc_t)(void* ptr, size_t size);

//\ VM Configuration
typedef struct ufold_vm_config_struct {
    ufold_vm_write_t write;      // writer for output (NULL: provided default)
    ufold_vm_realloc_t realloc;  // memory realloctor (NULL: provided default)
    size_t max_width;            // maximum columns allowed for text
    size_t tab_width;            // maximum columns allowed for tab
    bool keep_indentation;       // whether to keep indentation for wrapped text
    bool break_at_spaces;        // whether to break lines at spaces
    bool truncate_bytes;         // whether to count bytes rather than columns
} ufold_vm_config_t;

/*\
 / DESCRIPTION
 /   Create a new VM for line wrapping.
 /
 / PARAMETERS
 /   options --> VM options
 /
 / RETURN
 /   BEAF :: success
 /   NULL :: failure
\*/
ufold_vm_t* ufold_vm_new(ufold_vm_config_t config);

/*\
 / DESCRIPTION
 /   Free the memory used by the VM and its components.
\*/
void ufold_vm_free(ufold_vm_t* vm);

/*\
 / DESCRIPTION
 /   Output remaining text in the buffer and stop the VM.
 /
 / RETURN
 /    true :: success
 /   false :: failure
\*/
bool ufold_vm_stop(ufold_vm_t* vm);

/*\
 / DESCRIPTION
 /   Feed input into the VM and output transformed text.
 /
 / RETURN
 /    true :: success
 /   false :: failure
\*/
bool ufold_vm_feed(ufold_vm_t* vm, const void* input, const size_t size);

#endif  /* UFOLD_VM_H */
