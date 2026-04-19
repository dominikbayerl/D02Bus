#ifndef SML_H
#define SML_H

#include <stdint.h>
#include <stdbool.h>

/* VM Opcodes (Instructions) stored in the instruction stack */
typedef enum {
    /* Main Frame Opcodes */
    SML_OP_WAIT_SYNC,           /* Wait for 1B 1B 1B 1B */
    SML_OP_WAIT_VERSION,        /* Wait for 01 01 01 01 */
    
    /* Type-Length Opcodes */
    SML_OP_READ_TL,             /* Read Type/Length byte */
    SML_OP_READ_TL_EXT,         /* Read extended Type/Length byte */

    /* Branch Opcodes */
    SML_OP_BRANCH,              /* Conditionally branch based on exact match */

    /* Trailer Opcodes */
    SML_OP_TRAILER_PADDING,     /* Consume trailing 00 bytes */
    SML_OP_TRAILER_ESCAPE,      /* Consume 1B 1B 1B 1B */
    SML_OP_TRAILER_EOF,         /* Consume 1A XX CRC1 CRC2 */

    /* Data Opcodes */
    SML_OP_READ_BYTES,          /* Read N arbitrary bytes into memory */
    SML_OP_READ_LIST,           /* Expand list elements based on parsed length */

    /* Output Operations emitted to the main program */
    SML_OP_OUT_U1,
    SML_OP_OUT_U2,
    SML_OP_OUT_U4,
    SML_OP_OUT_U8,
    SML_OP_OUT_I1,
    SML_OP_OUT_I2,
    SML_OP_OUT_I4,
    SML_OP_OUT_I8,
    SML_OP_OUT_STR,
    SML_OP_OUT_LIST_START,
    SML_OP_OUT_LIST_END,
    SML_OP_OUT_EOF,

    SML_OP_DONE                 /* Parsing finished */
} sml_opcode_t;

/* Types of items parsed from TL fields */
typedef enum {
    SML_TYPE_OCTET_STRING = 0x00,
    SML_TYPE_BOOLEAN      = 0x40,
    SML_TYPE_INTEGER      = 0x50,
    SML_TYPE_UNSIGNED     = 0x60,
    SML_TYPE_LIST         = 0x70
} sml_type_t;

/* VM state structure */
typedef struct {
    uint8_t memory[32];         /* 32-byte memory filled byte-by-byte */
    uint8_t stack[32];          /* 32-byte instruction stack */
    int8_t  sp;                 /* Stack pointer */
    uint8_t mp;                 /* Memory pointer */

    /* Generic Registers */
    uint16_t reg_a;             /* Used for parsed length/elements */
    uint16_t reg_b;             /* Used for remaining bytes to read */
    uint16_t reg_c;             /* Multi-purpose (Current Type) */
    uint16_t reg_d;             /* Used for CRC16 calculation */

    /* Current Output Opcode */
    sml_opcode_t out_op;        
    uint8_t out_len;            /* Output length (bytes valid in memory) */
} sml_vm_t;

/* VM Initialization */
void sml_vm_init(sml_vm_t* vm);

/* Push an instruction to the stack */
void sml_vm_push(sml_vm_t* vm, uint8_t op);

/* Pop an instruction from the stack */
uint8_t sml_vm_pop(sml_vm_t* vm);

/* Feed one byte into the VM. Returns true if an output instruction was generated. */
bool sml_vm_step(sml_vm_t* vm, uint8_t b);

#endif /* SML_H */
