#include "sml.h"

/* SML uses X.25 / CRC-16-CCITT (Polynomial 0x1021, LSB first (reflected)) */
static void sml_crc16(uint16_t* crc, uint8_t b) {
    *crc ^= b;
    for (int i = 0; i < 8; i++) {
        if (*crc & 1) {
            *crc = (*crc >> 1) ^ 0x8408;
        } else {
            *crc >>= 1;
        }
    }
}

void sml_vm_init(sml_vm_t* vm) {
    if(!vm) return;
    vm->sp = -1;
    vm->mp = 0;
    vm->reg_a = 0;
    vm->reg_b = 0;
    vm->reg_c = 0;
    vm->reg_d = 0xFFFF; /* CRC init */
    vm->out_op = SML_OP_DONE;
    vm->out_len = 0;
}

void sml_vm_push(sml_vm_t* vm, uint8_t op) {
    if(vm->sp < 31) {
        vm->stack[++vm->sp] = op;
    }
}

uint8_t sml_vm_pop(sml_vm_t* vm) {
    if(vm->sp >= 0) {
        return vm->stack[vm->sp--];
    }
    return SML_OP_DONE;
}

bool sml_vm_step(sml_vm_t* vm, uint8_t b) {
    sml_crc16(&vm->reg_d, b);

eval_op:
    if (vm->sp < 0) return false;

    uint8_t op = vm->stack[vm->sp];

    switch(op) {
        case SML_OP_BRANCH: {
            sml_vm_pop(vm); /* Pop BRANCH itself */
            uint8_t cond = sml_vm_pop(vm);
            uint8_t match_op = sml_vm_pop(vm);
            uint8_t alt_op = sml_vm_pop(vm);

            if (b == cond) {
                /* Prepare structure to re-evaluate branch for subsequent items */
                sml_vm_push(vm, alt_op);
                sml_vm_push(vm, match_op);
                sml_vm_push(vm, cond);
                sml_vm_push(vm, SML_OP_BRANCH);

                /* Push execution for the matching byte */
                sml_vm_push(vm, match_op);
            } else {
                /* Push fallback branch */
                sml_vm_push(vm, alt_op);
            }
            
            /* Re-evaluate the same byte against the newly pushed top instruction */
            goto eval_op;
        }
        case SML_OP_WAIT_SYNC: {
            if (b == 0x1B) {
                vm->reg_a++;
                if (vm->reg_a == 4) {
                    sml_vm_pop(vm); /* Pop SYNC */
                    vm->reg_a = 0;
                }
            } else {
                vm->reg_a = 0;
            }
            break;
        }
        case SML_OP_WAIT_VERSION: {
            if (b == 0x01) {
                vm->reg_a++;
                if (vm->reg_a == 4) {
                    sml_vm_pop(vm); /* Pop VERSION */
                    vm->reg_a = 0;
                }
            } else {
                vm->reg_a = 0;
            }
            break;
        }
        case SML_OP_READ_TL: {
            if (b == 0x00) {
                /* Empty / Optional Element */
                sml_vm_pop(vm);
                vm->out_op = SML_OP_OUT_STR; /* Yield a string with 0 elements */
                vm->out_len = 0;
                return true;
            }
            
            uint8_t type = b & 0x70;
            uint8_t len = b & 0x0F;
            
            if (b & 0x80) {
                /* Multi-byte length */
                vm->reg_c = type; /* Save type */
                vm->reg_a = len;  /* First part of length */
                vm->stack[vm->sp] = SML_OP_READ_TL_EXT; /* Replace OP */
            } else {
                sml_vm_pop(vm);
                /* Evaluate Type and Length */
                if (type == SML_TYPE_LIST) {
                    vm->out_op = SML_OP_OUT_LIST_START;
                    vm->out_len = len;
                    /* Push N list elements to read */
                    for (int i = 0; i < len; i++) {
                        sml_vm_push(vm, SML_OP_READ_TL);
                    }
                    return true; /* Trigger out callback */
                } else {
                    /* Read primitive */
                    len--; /* len includes TL byte itself */
                    if (len > 0) {
                        vm->reg_b = len;
                        vm->reg_c = type;
                        vm->mp = 0; /* Reset memory pointer */
                        sml_vm_push(vm, SML_OP_READ_BYTES);
                    } else {
                        /* 0 byte data fields */
                        vm->out_op = SML_OP_OUT_STR; // Emit empty string or bool
                        vm->out_len = 0;
                        return true;
                    }
                }
            }
            break;
        }
        case SML_OP_READ_TL_EXT: {
            vm->reg_a = (vm->reg_a << 4) | (b & 0x0F);
            if (!(b & 0x80)) {
                /* Last length byte */
                sml_vm_pop(vm);
                uint8_t type = vm->reg_c;
                uint16_t len = vm->reg_a;
                
                if (type == SML_TYPE_LIST) {
                    vm->out_op = SML_OP_OUT_LIST_START;
                    vm->out_len = len; /* Should clip if > 255 but good for simple lists */
                    for (int i = 0; i < len; i++) {
                        sml_vm_push(vm, SML_OP_READ_TL);
                    }
                    return true;
                } else {
                    len--; /* includes total length of TL bytes */
                    if (len > 0) {
                        vm->reg_b = len;
                        vm->mp = 0;
                        sml_vm_push(vm, SML_OP_READ_BYTES);
                    }
                }
            }
            break;
        }
        case SML_OP_READ_BYTES: {
            if (vm->mp < 32) {
                vm->memory[vm->mp++] = b;
            }
            vm->reg_b--;
            if (vm->reg_b == 0) {
                sml_vm_pop(vm);
                
                /* Generate emit based on type in reg_c */
                if (vm->reg_c == SML_TYPE_OCTET_STRING) {
                    vm->out_op = SML_OP_OUT_STR;
                } else if (vm->reg_c == SML_TYPE_UNSIGNED) {
                    if (vm->mp == 1) vm->out_op = SML_OP_OUT_U1;
                    else if (vm->mp == 2) vm->out_op = SML_OP_OUT_U2;
                    else if (vm->mp == 4) vm->out_op = SML_OP_OUT_U4;
                    else if (vm->mp == 8) vm->out_op = SML_OP_OUT_U8;
                } else if (vm->reg_c == SML_TYPE_INTEGER) {
                    if (vm->mp == 1) vm->out_op = SML_OP_OUT_I1;
                    else if (vm->mp == 2) vm->out_op = SML_OP_OUT_I2;
                    else if (vm->mp == 4) vm->out_op = SML_OP_OUT_I4;
                    else if (vm->mp == 8) vm->out_op = SML_OP_OUT_I8;
                }
                vm->out_len = vm->mp;
                return true;
            }
            break;
        }
        case SML_OP_TRAILER_PADDING: {
            if (b == 0x00) {
                /* Consumed padding */
            } else if (b == 0x1B) {
                /* First byte of escape sequence, transition */
                vm->stack[vm->sp] = SML_OP_TRAILER_ESCAPE;
                vm->reg_a = 1;
            } else {
                sml_vm_pop(vm); /* Unexpected byte, abort trailer padding */
            }
            break;
        }
        case SML_OP_TRAILER_ESCAPE: {
            if (b == 0x1B) {
                vm->reg_a++;
                if (vm->reg_a == 4) {
                    sml_vm_pop(vm); /* Consumed 4x 1B escape */
                    vm->reg_a = 0;  /* Reset for EOF */
                }
            } else {
                sml_vm_pop(vm); /* Abort if sequence broken */
            }
            break;
        }
        case SML_OP_TRAILER_EOF: {
            if (vm->reg_a == 0) {
                if (b == 0x1A) vm->reg_a++;
                else sml_vm_pop(vm); /* Not EOF marker */
            } else if (vm->reg_a == 1) {
                vm->reg_b = b; /* Padding byte count */
                vm->reg_a++;
            } else if (vm->reg_a == 2) {
                /* CRC byte 1 */
                vm->reg_a++;
            } else if (vm->reg_a == 3) {
                /* CRC byte 2 */
                sml_vm_pop(vm);
                
                vm->out_op = SML_OP_OUT_EOF;
                vm->out_len = 0;
                return true;
            }
            break;
        }
        case SML_OP_DONE:
            break;
    }
    
    return false;
}
