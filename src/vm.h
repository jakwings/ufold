#ifndef UFOLD_VM_H
#define UFOLD_VM_H

#include <stddef.h>
#include <stdint.h>
#include "stdbool.h"

//\ Virtual State Machine
typedef struct ufold_vm_struct ufold_vm_t;

//\ Writer for Output
typedef bool (*ufold_vm_write_t)(const void* ptr, size_t size);

//\ Memory Reallocator
typedef void* (*ufold_vm_realloc_t)(void* ptr, size_t size);

//\ VM Configuration
typedef struct ufold_vm_config_struct {
    ufold_vm_write_t write;      // writer for output (NULL: provided default)
    ufold_vm_realloc_t realloc;  // memory reallocator (NULL: provided default)
    size_t max_width;            // maximum columns allowed for text
    size_t tab_width;            // maximum columns allowed for tab
    char* punctuation;           // hanging punctuation
    bool hang_punctuation;       // whether to hang punctuation at line start
    bool keep_indentation;       // whether to keep indentation for wrapped text
    bool break_at_spaces;        // whether to break lines at spaces
    bool ascii_mode;             // whether to count bytes rather than columns
    bool line_buffered;          // whether to support line-buffered output
    // TODO: --reserve=width
} ufold_vm_config_t;

/*\
 / DESCRIPTION
 /   Reset the settings' value to a normal state.
 /
 / PARAMETERS
 /   *config --> VM settings
\*/
void ufold_vm_config_init(ufold_vm_config_t* config);

/*\
 / DESCRIPTION
 /   Create a new VM for line wrapping.
 /
 / PARAMETERS
 /   *config --> VM settings
 /
 / RETURN
 /   BEAF :: success
 /   NULL :: failure
\*/
ufold_vm_t* ufold_vm_new(const ufold_vm_config_t* config);

/*\
 / DESCRIPTION
 /   Free the memory used by the VM and its components.
\*/
void ufold_vm_free(ufold_vm_t* vm);

/*\
 / DESCRIPTION
 /   Output remaining text in the buffer and stop the VM.
 /   A second stop will always succeed.
 /
 / RETURN
 /    true :: success
 /   false :: failure
\*/
bool ufold_vm_stop(ufold_vm_t* vm);

/*\
 / DESCRIPTION
 /   Flush all buffered output of the VM.
 /   Flushing an already stopped VM will return false.
 /
 / RETURN
 /    true :: success
 /   false :: failure
\*/
bool ufold_vm_flush(ufold_vm_t* vm);

/*\
 / DESCRIPTION
 /   Feed input into the VM and output transformed text.
 /   Feeding an already stopped VM will return false.
 /
 / PARAMETERS
 /   input --> address of input
 /    size --> size of input in bytes
 /
 / RETURN
 /    true :: success
 /   false :: failure
\*/
bool ufold_vm_feed(ufold_vm_t* vm, const void* input, size_t size);

#endif  /* UFOLD_VM_H */
