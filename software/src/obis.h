#ifndef OBIS_H
#define OBIS_H

#include <stdint.h>
#include <stdbool.h>

#include "sml.h"

#define OBIS_MAX_LIST_DEPTH 12
#define OBIS_MAX_VALUE_BYTES 32

typedef struct {
    uint8_t remaining;
    uint8_t index;
} obis_list_frame_t;

typedef struct {
    bool in_entry;
    uint8_t field_index;
    bool field_open;
    uint8_t field_pending;

    bool have_obis;
    uint8_t obis[6];

    bool have_unit;
    uint16_t unit;

    bool have_scaler;
    int8_t scaler;

    bool have_value;
    sml_opcode_t value_type;
    uint8_t value_len;
    uint8_t value[OBIS_MAX_VALUE_BYTES];
} obis_entry_state_t;

typedef struct {
    bool have_obis;
    uint8_t obis[6];

    bool have_unit;
    uint16_t unit;

    bool have_scaler;
    int8_t scaler;

    bool have_value;
    sml_opcode_t value_type;
    uint8_t value_len;
    uint8_t value[OBIS_MAX_VALUE_BYTES];
} obis_entry_out_t;

typedef struct {
    obis_list_frame_t stack[OBIS_MAX_LIST_DEPTH];
    uint8_t depth;

    bool in_message;
    uint8_t message_depth;

    bool in_choice;
    uint8_t choice_depth;
    uint16_t message_type;

    bool in_getlist;
    uint8_t getlist_depth;

    bool in_val_list;
    uint8_t val_list_depth;

    obis_entry_state_t entry;

    bool entry_ready;
    obis_entry_out_t entry_out;
} obis_parser_t;

void obis_parser_init(obis_parser_t* st);
void obis_parser_reset(obis_parser_t* st);
void obis_parser_handle_output(obis_parser_t* st, const sml_vm_t* vm);
bool obis_parser_pop_entry(obis_parser_t* st, obis_entry_out_t* out);

#endif /* OBIS_H */
