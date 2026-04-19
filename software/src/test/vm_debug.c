#include <stdio.h>
#include <stdlib.h>
#include "sml.h"

const char* op_to_str(sml_opcode_t op) {
    switch(op) {
        case SML_OP_WAIT_SYNC: return "WAIT_SYNC";
        case SML_OP_WAIT_VERSION: return "WAIT_VERSION";
        case SML_OP_READ_TL: return "READ_TL";
        case SML_OP_READ_TL_EXT: return "READ_TL_EXT";
        case SML_OP_BRANCH: return "BRANCH";
        case SML_OP_READ_BYTES: return "READ_BYTES";
        case SML_OP_READ_LIST: return "READ_LIST";
        case SML_OP_OUT_U1: return "OUT_U1";
        case SML_OP_OUT_U2: return "OUT_U2";
        case SML_OP_OUT_U4: return "OUT_U4";
        case SML_OP_OUT_U8: return "OUT_U8";
        case SML_OP_OUT_I1: return "OUT_I1";
        case SML_OP_OUT_I2: return "OUT_I2";
        case SML_OP_OUT_I4: return "OUT_I4";
        case SML_OP_OUT_I8: return "OUT_I8";
        case SML_OP_OUT_STR: return "OUT_STR";
        case SML_OP_OUT_LIST_START: return "OUT_LIST_START";
        case SML_OP_OUT_LIST_END: return "OUT_LIST_END";
        case SML_OP_OUT_EOF: return "OUT_EOF";
        case SML_OP_TRAILER_PADDING: return "TRAIL_PAD";
        case SML_OP_TRAILER_ESCAPE: return "TRAIL_ESC";
        case SML_OP_TRAILER_EOF: return "TRAIL_EOF";
        case SML_OP_DONE: return "DONE";
        default: return "UNKNOWN";
    }
}

int main(int argc, char** argv) {
    if(argc != 2) {
        printf("Usage: %s <sml_frame.bin>\n", argv[0]);
        return 1;
    }

    FILE* f = fopen(argv[1], "rb");
    if(!f) {
        printf("Failed to open %s\n", argv[1]);
        return 1;
    }

    sml_vm_t vm;
    sml_vm_init(&vm);

    // Some test dumps contain a full escape/version header. 
    // We can push the standard frame start: Wait Sync (1B x4) -> Wait Version (01 x4) -> [Branch messages] -> Trailer
    sml_vm_push(&vm, SML_OP_TRAILER_EOF);
    
    // Setup message loop branch handler
    sml_vm_push(&vm, SML_OP_TRAILER_PADDING); /* Alternative/Fallthrough */
    sml_vm_push(&vm, SML_OP_READ_TL); /* Match operation */
    sml_vm_push(&vm, 0x76); /* SmlMessage lists have 6 elements (0x76) */
    sml_vm_push(&vm, SML_OP_BRANCH); /* Conditionally branch op */

    sml_vm_push(&vm, SML_OP_WAIT_VERSION);
    sml_vm_push(&vm, SML_OP_WAIT_SYNC);

    printf("=== SML VM Debug ===\n");
    printf("File: %s\n", argv[1]);
    printf("--------------------------------------------------\n");

    int byte_count = 0;
    int eof_state = 0;
    int pad_count = 0;

    int ch;
    // Process SML frame until VM stack is empty
    while(vm.sp >= 0 && (ch = fgetc(f)) != EOF) {
        uint8_t b = (uint8_t)ch;
        byte_count++;
        
        bool triggered = sml_vm_step(&vm, b);
        
        if(triggered) {
            printf("[Byte %4d] op=%-14s len=%-3d ", byte_count, op_to_str(vm.out_op), vm.out_len);
            if(vm.out_op == SML_OP_OUT_EOF) {
                printf("data=[Pad: %d bytes]", vm.reg_b);
            } else if(vm.out_op >= SML_OP_OUT_U1 && vm.out_op <= SML_OP_OUT_STR) {
                printf("data=[ ");
                for(int j=0; j<vm.out_len; j++) {
                    printf("%02X ", vm.memory[j]);
                }
                printf("]");
            }
            printf("\n");
            
            if (vm.out_op == SML_OP_OUT_EOF) {
                uint16_t final_crc = vm.reg_d; 
                // Proper X.25 residual verification is 0xF0B8.
                if (final_crc == 0xF0B8) {
                    printf("----> CRC is VALID (Residual 0xF0B8)\n");
                } else {
                    printf("----> CRC is INVALID (Residual 0x%04X, Expected 0xF0B8)\n", final_crc);
                }
            }
        }
    }

    // Capture the remainder of the stream for the hexdump (if any)
    uint8_t remainder[4096];
    int rem_len = 0;
    while((ch = fgetc(f)) != EOF) {
        if (rem_len < (int)sizeof(remainder)) {
            remainder[rem_len++] = (uint8_t)ch;
        }
    }

    if (rem_len > 0) {
        printf("--------------------------------------------------\n");
        printf("Remainder Hexdump (%d bytes):\n", rem_len);
        for(int i = 0; i < rem_len; i++) {
            printf("%02X ", remainder[i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }
        if (rem_len % 16 != 0) printf("\n");
        printf("--------------------------------------------------\n");
    }

    // Process the remainder through the VM trailer state machine
    for(int i = 0; i < rem_len; i++) {
        uint8_t b = remainder[i];
        byte_count++;
        
        // Feed into VM just for CRC calculation
        bool triggered = sml_vm_step(&vm, b);
        
        if(triggered) {
            printf("[Byte %4d] op=%-14s len=%-3d ", byte_count, op_to_str(vm.out_op), vm.out_len);
            if(vm.out_op == SML_OP_OUT_EOF) {
                printf("data=[Pad: %d bytes]", vm.reg_b);
            } else if(vm.out_op >= SML_OP_OUT_U1 && vm.out_op <= SML_OP_OUT_STR) {
                printf("data=[ ");
                for(int j=0; j<vm.out_len; j++) {
                    printf("%02X ", vm.memory[j]);
                }
                printf("]");
            }
            printf("\n");
            
            if (vm.out_op == SML_OP_OUT_EOF) {
                uint16_t final_crc = vm.reg_d; 
                // Proper X.25 residual verification is 0xF0B8.
                if (final_crc == 0xF0B8) {
                    printf("----> CRC is VALID (Residual 0xF0B8)\n");
                } else {
                    printf("----> CRC is INVALID (Residual 0x%04X, Expected 0xF0B8)\n", final_crc);
                }
            }
        }
    }
    printf("--------------------------------------------------\n");
    printf("Final CRC calculation: 0x%04X\n", vm.reg_d);
    printf("Remaining stack pointer: %d\n", vm.sp);

    fclose(f);
    return 0;
}
