#include "obis.h"

#include <string.h>

static bool obis_is_primitive_out(sml_opcode_t op) {
    switch (op) {
        case SML_OP_OUT_STR:
        case SML_OP_OUT_U1:
        case SML_OP_OUT_U2:
        case SML_OP_OUT_U4:
        case SML_OP_OUT_U8:
        case SML_OP_OUT_I1:
        case SML_OP_OUT_I2:
        case SML_OP_OUT_I4:
        case SML_OP_OUT_I8:
            return true;
        default:
            return false;
    }
}

static bool obis_is_unsigned_out(sml_opcode_t op) {
    switch (op) {
        case SML_OP_OUT_U1:
        case SML_OP_OUT_U2:
        case SML_OP_OUT_U4:
        case SML_OP_OUT_U8:
            return true;
        default:
            return false;
    }
}

static bool obis_is_signed_out(sml_opcode_t op) {
    switch (op) {
        case SML_OP_OUT_I1:
        case SML_OP_OUT_I2:
        case SML_OP_OUT_I4:
        case SML_OP_OUT_I8:
            return true;
        default:
            return false;
    }
}

static uint32_t obis_read_be_uint32(const uint8_t* data, uint8_t len) {
    uint32_t value = 0;
    uint8_t max_len = (len > 4) ? 4 : len;
    for (uint8_t i = 0; i < max_len; i++) {
        value = (value << 8) | data[i];
    }
    return value;
}

static int32_t obis_read_be_int32(const uint8_t* data, uint8_t len) {
    if (len == 0) {
        return 0;
    }

    uint8_t max_len = (len > 4) ? 4 : len;
    uint32_t value = obis_read_be_uint32(data, max_len);
    uint32_t sign_bit = 1UL << ((max_len * 8U) - 1U);
    if (value & sign_bit) {
        uint32_t mask = ~((1UL << (max_len * 8U)) - 1U);
        value |= mask;
    }

    return (int32_t)value;
}

static void obis_entry_reset(obis_entry_state_t* st) {
    memset(st, 0, sizeof(*st));
    st->value_type = SML_OP_DONE;
}

static void obis_store_entry(obis_parser_t* st, const obis_entry_state_t* src) {
    if (!st) {
        return;
    }

    st->entry_ready = true;
    st->entry_out.have_obis = src->have_obis;
    memcpy(st->entry_out.obis, src->obis, sizeof(st->entry_out.obis));
    st->entry_out.have_unit = src->have_unit;
    st->entry_out.unit = src->unit;
    st->entry_out.have_scaler = src->have_scaler;
    st->entry_out.scaler = src->scaler;
    st->entry_out.have_value = src->have_value;
    st->entry_out.value_type = src->value_type;
    st->entry_out.value_len = src->value_len;
    memcpy(st->entry_out.value, src->value, sizeof(st->entry_out.value));
}

static void obis_entry_start_field(obis_entry_state_t* st, const sml_vm_t* vm) {
    st->field_open = true;
    st->field_pending = (vm->out_op == SML_OP_OUT_LIST_START) ? vm->out_len : 0;

    if (st->field_index == 0) {
        if (vm->out_op == SML_OP_OUT_STR && vm->out_len == 6) {
            memcpy(st->obis, vm->memory, 6);
            st->have_obis = true;
        } else {
            obis_entry_reset(st);
        }
        return;
    }

    if (st->field_index == 3) {
        if (obis_is_unsigned_out(vm->out_op)) {
            st->unit = (uint16_t)obis_read_be_uint32(vm->memory, vm->out_len);
            st->have_unit = true;
        }
        return;
    }

    if (st->field_index == 4) {
        if (obis_is_signed_out(vm->out_op) || obis_is_unsigned_out(vm->out_op)) {
            st->scaler = (int8_t)obis_read_be_int32(vm->memory, vm->out_len);
            st->have_scaler = true;
        }
        return;
    }

    if (st->field_index == 5) {
        st->value_type = vm->out_op;
        st->value_len = 0;
        st->have_value = false;

        if (obis_is_primitive_out(vm->out_op)) {
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

static void obis_entry_continue_field(obis_entry_state_t* st, const sml_vm_t* vm) {
    if (st->field_pending > 0) {
        st->field_pending--;
    }
    if (vm->out_op == SML_OP_OUT_LIST_START) {
        st->field_pending += vm->out_len;
    }
}

static void obis_entry_finish_field(obis_parser_t* parser, obis_entry_state_t* st) {
    st->field_open = false;
    st->field_index++;

    if (st->field_index >= 7) {
        if (st->have_obis && st->have_value) {
            obis_store_entry(parser, st);
        }
        obis_entry_reset(st);
    }
}

static void obis_entry_handle_output(obis_parser_t* parser, obis_entry_state_t* st, const sml_vm_t* vm) {
    if (!st->in_entry) {
        return;
    }

    if (!st->field_open) {
        obis_entry_start_field(st, vm);
        if (st->field_open && st->field_pending == 0) {
            obis_entry_finish_field(parser, st);
        }
        return;
    }

    obis_entry_continue_field(st, vm);
    if (st->field_pending == 0) {
        obis_entry_finish_field(parser, st);
    }
}

static void obis_update_contexts_for_depth(obis_parser_t* st) {
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

static void obis_list_push(obis_parser_t* st, uint8_t len) {
    if (st->depth >= OBIS_MAX_LIST_DEPTH) {
        return;
    }
    st->stack[st->depth].remaining = len;
    st->stack[st->depth].index = 0;
    st->depth++;
}

static void obis_list_consume_element(obis_parser_t* st) {
    if (st->depth == 0) {
        return;
    }

    obis_list_frame_t* top = &st->stack[st->depth - 1];
    if (top->remaining > 0) {
        top->remaining--;
        top->index++;
    }

    while (st->depth > 0 && st->stack[st->depth - 1].remaining == 0) {
        st->depth--;
        obis_update_contexts_for_depth(st);
    }
}

static void obis_handle_list_start(obis_parser_t* st, const sml_vm_t* vm) {
    bool has_parent = st->depth > 0;
    uint8_t parent_index = has_parent ? st->stack[st->depth - 1].index : 0;
    uint8_t parent_depth = st->depth;

    bool enter_message = (!st->in_message && vm->out_len == 6);
    bool enter_choice = (st->in_message && parent_depth == st->message_depth && parent_index == 3 && vm->out_len == 2);
    bool enter_getlist = (st->in_choice && parent_depth == st->choice_depth && parent_index == 1 && st->message_type == 0x701 && vm->out_len == 7);
    bool enter_val_list = (st->in_getlist && parent_depth == st->getlist_depth && parent_index == 4);
    bool start_entry = (st->in_val_list && parent_depth == st->val_list_depth && vm->out_len == 7);

    if (st->entry.in_entry) {
        obis_entry_handle_output(st, &st->entry, vm);
    }

    if (has_parent) {
        obis_list_consume_element(st);
    }

    obis_list_push(st, vm->out_len);

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
        obis_entry_reset(&st->entry);
        st->entry.in_entry = true;
    }
}

static void obis_handle_primitive(obis_parser_t* st, const sml_vm_t* vm) {
    if (st->in_choice && st->depth == st->choice_depth) {
        obis_list_frame_t* top = &st->stack[st->depth - 1];
        if (top->index == 0 && obis_is_unsigned_out(vm->out_op)) {
            st->message_type = (uint16_t)obis_read_be_uint32(vm->memory, vm->out_len);
        }
    }

    if (st->entry.in_entry) {
        obis_entry_handle_output(st, &st->entry, vm);
    }

    obis_list_consume_element(st);
}

void obis_parser_init(obis_parser_t* st) {
    obis_parser_reset(st);
}

void obis_parser_reset(obis_parser_t* st) {
    if (!st) {
        return;
    }
    memset(st, 0, sizeof(*st));
    obis_entry_reset(&st->entry);
}

bool obis_parser_pop_entry(obis_parser_t* st, obis_entry_out_t* out) {
    if (!st || !out) {
        return false;
    }
    if (!st->entry_ready) {
        return false;
    }

    memcpy(out, &st->entry_out, sizeof(*out));
    st->entry_ready = false;
    return true;
}

void obis_parser_handle_output(obis_parser_t* st, const sml_vm_t* vm) {
    if (!st || !vm) {
        return;
    }

    if (vm->out_op == SML_OP_OUT_LIST_START) {
        obis_handle_list_start(st, vm);
        return;
    }
    if (obis_is_primitive_out(vm->out_op)) {
        obis_handle_primitive(st, vm);
    }
}
