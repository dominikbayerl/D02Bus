#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "sml.h"

#define MAX_LIST_DEPTH 16
#define MAX_VALUE_BYTES 32
#define MAX_OBIS_STR 32
#define MAX_VALUE_STR 64

typedef struct {
    uint16_t remaining;
    uint16_t index;
} list_frame_t;

typedef struct {
    bool in_entry;
    uint8_t field_index;
    bool field_open;
    uint16_t field_pending;

    bool have_obis;
    uint8_t obis[6];

    bool have_unit;
    uint16_t unit;

    bool have_scaler;
    int8_t scaler;

    bool have_value;
    sml_opcode_t value_type;
    uint8_t value_len;
    uint8_t value[MAX_VALUE_BYTES];
} entry_state_t;

typedef struct {
    list_frame_t stack[MAX_LIST_DEPTH];
    int depth;

    bool in_message;
    int message_depth;

    bool in_choice;
    int choice_depth;
    uint16_t message_type;

    bool in_getlist;
    int getlist_depth;

    bool in_val_list;
    int val_list_depth;

    entry_state_t entry;
} parse_state_t;

static void sml_vm_arm_for_stream(sml_vm_t* vm) {
    sml_vm_init(vm);

    sml_vm_push(vm, SML_OP_TRAILER_EOF);
    sml_vm_push(vm, SML_OP_TRAILER_PADDING);
    sml_vm_push(vm, SML_OP_READ_TL);
    sml_vm_push(vm, 0x76);
    sml_vm_push(vm, SML_OP_BRANCH);
    sml_vm_push(vm, SML_OP_WAIT_VERSION);
    sml_vm_push(vm, SML_OP_WAIT_SYNC);
}

static bool is_primitive_out(sml_opcode_t op) {
    return (op == SML_OP_OUT_STR) ||
           (op == SML_OP_OUT_U1) ||
           (op == SML_OP_OUT_U2) ||
           (op == SML_OP_OUT_U4) ||
           (op == SML_OP_OUT_U8) ||
           (op == SML_OP_OUT_I1) ||
           (op == SML_OP_OUT_I2) ||
           (op == SML_OP_OUT_I4) ||
           (op == SML_OP_OUT_I8);
}

static bool is_unsigned_out(sml_opcode_t op) {
    return (op == SML_OP_OUT_U1) ||
           (op == SML_OP_OUT_U2) ||
           (op == SML_OP_OUT_U4) ||
           (op == SML_OP_OUT_U8);
}

static bool is_signed_out(sml_opcode_t op) {
    return (op == SML_OP_OUT_I1) ||
           (op == SML_OP_OUT_I2) ||
           (op == SML_OP_OUT_I4) ||
           (op == SML_OP_OUT_I8);
}

static uint64_t read_be_uint(const uint8_t* data, uint8_t len) {
    uint64_t value = 0;
    for (uint8_t i = 0; i < len; i++) {
        value = (value << 8) | data[i];
    }
    return value;
}

static int64_t read_be_int(const uint8_t* data, uint8_t len) {
    if (len == 0) {
        return 0;
    }

    uint64_t value = read_be_uint(data, len);
    if (len < 8) {
        uint64_t sign_bit = 1ULL << ((len * 8) - 1);
        if (value & sign_bit) {
            uint64_t mask = ~((1ULL << (len * 8)) - 1);
            value |= mask;
        }
    }

    return (int64_t)value;
}

static void format_scaled_int64(char* out, size_t out_size, int64_t value, int8_t scaler) {
    char digits[32];
    size_t pos = 0;
    bool negative = value < 0;
    uint64_t abs_val = negative ? (uint64_t)(-value) : (uint64_t)value;

    snprintf(digits, sizeof(digits), "%llu", (unsigned long long)abs_val);

    if (negative && pos + 1 < out_size) {
        out[pos++] = '-';
    }

    if (scaler >= 0) {
        size_t len = strlen(digits);
        for (size_t i = 0; i < len && pos + 1 < out_size; i++) {
            out[pos++] = digits[i];
        }
        for (int i = 0; i < scaler && pos + 1 < out_size; i++) {
            out[pos++] = '0';
        }
        out[pos] = '\0';
        return;
    }

    int scale = -scaler;
    size_t len = strlen(digits);
    if (len <= (size_t)scale) {
        if (pos + 1 < out_size) {
            out[pos++] = '0';
        }
        if (pos + 1 < out_size) {
            out[pos++] = '.';
        }
        for (int i = 0; i < (scale - (int)len) && pos + 1 < out_size; i++) {
            out[pos++] = '0';
        }
        for (size_t i = 0; i < len && pos + 1 < out_size; i++) {
            out[pos++] = digits[i];
        }
        out[pos] = '\0';
        return;
    }

    size_t int_len = len - (size_t)scale;
    for (size_t i = 0; i < int_len && pos + 1 < out_size; i++) {
        out[pos++] = digits[i];
    }
    if (pos + 1 < out_size) {
        out[pos++] = '.';
    }
    for (size_t i = int_len; i < len && pos + 1 < out_size; i++) {
        out[pos++] = digits[i];
    }
    out[pos] = '\0';
}

static const char* unit_to_str(uint16_t unit) {
    switch (unit) {
        case 27:
            return "W";
        case 30:
            return "Wh";
        default:
            return "";
    }
}

static void obis_to_ascii(const uint8_t obis[6], char* out, size_t out_size) {
    snprintf(out, out_size, "%u-%u:%u.%u.%u*%u",
             obis[0], obis[1], obis[2], obis[3], obis[4], obis[5]);
}

static void bytes_to_ascii(const uint8_t* data, uint8_t len, char* out, size_t out_size) {
    size_t pos = 0;
    for (uint8_t i = 0; i < len && pos + 1 < out_size; i++) {
        unsigned char c = data[i];
        out[pos++] = isprint(c) ? (char)c : '.';
    }
    out[pos] = '\0';
}

static void entry_reset(entry_state_t* st) {
    memset(st, 0, sizeof(*st));
    st->value_type = SML_OP_DONE;
}

static void emit_entry_line(const entry_state_t* st) {
    char obis_str[MAX_OBIS_STR];
    char value_str[MAX_VALUE_STR];
    const char* unit_str = "";

    obis_to_ascii(st->obis, obis_str, sizeof(obis_str));

    if (st->value_type == SML_OP_OUT_STR) {
        bytes_to_ascii(st->value, st->value_len, value_str, sizeof(value_str));
    } else if (is_unsigned_out(st->value_type)) {
        uint64_t value = read_be_uint(st->value, st->value_len);
        int8_t scaler = st->have_scaler ? st->scaler : 0;
        format_scaled_int64(value_str, sizeof(value_str), (int64_t)value, scaler);
    } else if (is_signed_out(st->value_type)) {
        int64_t value = read_be_int(st->value, st->value_len);
        int8_t scaler = st->have_scaler ? st->scaler : 0;
        format_scaled_int64(value_str, sizeof(value_str), value, scaler);
    } else {
        return;
    }

    if (st->have_unit) {
        unit_str = unit_to_str(st->unit);
    }

    printf("%s#%s#%s\n", obis_str, value_str, unit_str);
}

static void entry_start_field(entry_state_t* st, const sml_vm_t* vm) {
    st->field_open = true;
    st->field_pending = (vm->out_op == SML_OP_OUT_LIST_START) ? vm->out_len : 0;

    if (st->field_index == 0) {
        if (vm->out_op == SML_OP_OUT_STR && vm->out_len == 6) {
            memcpy(st->obis, vm->memory, 6);
            st->have_obis = true;
        } else {
            entry_reset(st);
        }
        return;
    }

    if (st->field_index == 3) {
        if (is_unsigned_out(vm->out_op)) {
            st->unit = (uint16_t)read_be_uint(vm->memory, vm->out_len);
            st->have_unit = true;
        }
        return;
    }

    if (st->field_index == 4) {
        if (is_signed_out(vm->out_op) || is_unsigned_out(vm->out_op)) {
            st->scaler = (int8_t)read_be_int(vm->memory, vm->out_len);
            st->have_scaler = true;
        }
        return;
    }

    if (st->field_index == 5) {
        st->value_type = vm->out_op;
        st->value_len = 0;
        st->have_value = false;

        if (is_primitive_out(vm->out_op)) {
            uint8_t copy_len = vm->out_len;
            if (copy_len > sizeof(st->value)) {
                copy_len = sizeof(st->value);
            }
            memcpy(st->value, vm->memory, copy_len);
            st->value_len = copy_len;
            st->have_value = true;
        }
    }
}

static void entry_continue_field(entry_state_t* st, const sml_vm_t* vm) {
    if (st->field_pending > 0) {
        st->field_pending--;
    }
    if (vm->out_op == SML_OP_OUT_LIST_START) {
        st->field_pending += vm->out_len;
    }
}

static void entry_finish_field(entry_state_t* st) {
    st->field_open = false;
    st->field_index++;

    if (st->field_index >= 7) {
        if (st->have_obis && st->have_value) {
            emit_entry_line(st);
        }
        entry_reset(st);
    }
}

static void entry_handle_output(entry_state_t* st, const sml_vm_t* vm) {
    if (!st->in_entry) {
        return;
    }

    if (!st->field_open) {
        entry_start_field(st, vm);
        if (st->field_open && st->field_pending == 0) {
            entry_finish_field(st);
        }
        return;
    }

    entry_continue_field(st, vm);
    if (st->field_pending == 0) {
        entry_finish_field(st);
    }
}

static void parse_state_reset(parse_state_t* st) {
    memset(st, 0, sizeof(*st));
    entry_reset(&st->entry);
}

static void list_push(parse_state_t* st, uint16_t len) {
    if (st->depth >= MAX_LIST_DEPTH) {
        return;
    }
    st->stack[st->depth].remaining = len;
    st->stack[st->depth].index = 0;
    st->depth++;
}

static void update_contexts_for_depth(parse_state_t* st) {
    if (st->in_val_list && st->depth < st->val_list_depth) {
        st->in_val_list = false;
        st->val_list_depth = 0;
    }
    if (st->in_getlist && st->depth < st->getlist_depth) {
        st->in_getlist = false;
        st->getlist_depth = 0;
    }
    if (st->in_choice && st->depth < st->choice_depth) {
        st->in_choice = false;
        st->choice_depth = 0;
        st->message_type = 0;
    }
    if (st->in_message && st->depth < st->message_depth) {
        st->in_message = false;
        st->message_depth = 0;
    }
}

static void list_consume_element(parse_state_t* st) {
    if (st->depth <= 0) {
        return;
    }

    list_frame_t* top = &st->stack[st->depth - 1];
    if (top->remaining > 0) {
        top->remaining--;
        top->index++;
    }

    while (st->depth > 0 && st->stack[st->depth - 1].remaining == 0) {
        st->depth--;
        update_contexts_for_depth(st);
    }
}

static void handle_list_start(parse_state_t* st, const sml_vm_t* vm) {
    bool has_parent = st->depth > 0;
    uint16_t parent_index = has_parent ? st->stack[st->depth - 1].index : 0;
    int parent_depth = st->depth;

    bool enter_message = (!st->in_message && vm->out_len == 6);
    bool enter_choice = (st->in_message && parent_depth == st->message_depth && parent_index == 3 && vm->out_len == 2);
    bool enter_getlist = (st->in_choice && parent_depth == st->choice_depth && parent_index == 1 && st->message_type == 0x701 && vm->out_len == 7);
    bool enter_val_list = (st->in_getlist && parent_depth == st->getlist_depth && parent_index == 4);
    bool start_entry = (st->in_val_list && parent_depth == st->val_list_depth && vm->out_len == 7);

    if (st->entry.in_entry) {
        entry_handle_output(&st->entry, vm);
    }

    if (has_parent) {
        list_consume_element(st);
    }

    list_push(st, vm->out_len);

    if (enter_message) {
        st->in_message = true;
        st->message_depth = st->depth;
    }

    if (enter_choice) {
        st->in_choice = true;
        st->choice_depth = st->depth;
        st->message_type = 0;
    }

    if (enter_getlist) {
        st->in_getlist = true;
        st->getlist_depth = st->depth;
    }

    if (enter_val_list) {
        st->in_val_list = true;
        st->val_list_depth = st->depth;
    }

    if (start_entry) {
        entry_reset(&st->entry);
        st->entry.in_entry = true;
    }
}

static void handle_primitive(parse_state_t* st, const sml_vm_t* vm) {
    if (st->in_choice && st->depth == st->choice_depth) {
        list_frame_t* top = &st->stack[st->depth - 1];
        if (top->index == 0 && is_unsigned_out(vm->out_op)) {
            st->message_type = (uint16_t)read_be_uint(vm->memory, vm->out_len);
        }
    }

    if (st->entry.in_entry) {
        entry_handle_output(&st->entry, vm);
    }

    list_consume_element(st);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("Usage: %s <sml_frame.bin>\n", argv[0]);
        return 1;
    }

    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        printf("Failed to open %s\n", argv[1]);
        return 1;
    }

    sml_vm_t vm;
    parse_state_t st;
    parse_state_reset(&st);
    sml_vm_arm_for_stream(&vm);

    int ch;
    while ((ch = fgetc(f)) != EOF) {
        uint8_t b = (uint8_t)ch;
        if (sml_vm_step(&vm, b)) {
            if (vm.out_op == SML_OP_OUT_EOF) {
                parse_state_reset(&st);
                sml_vm_arm_for_stream(&vm);
                continue;
            }
            if (vm.out_op == SML_OP_OUT_LIST_START) {
                handle_list_start(&st, &vm);
            }
            if (is_primitive_out(vm.out_op)) {
                handle_primitive(&st, &vm);
            }
        }
    }

    fclose(f);
    return 0;
}
