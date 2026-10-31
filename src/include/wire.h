#ifndef WIRE_H
#define WIRE_H

#include<unistd.h>
#include<stdint.h>
#include<stdbool.h>

typedef struct {
  // These are i32 and not u32 to match the definitions in the
  // MongoDB wire protocol
  int32_t message_length; 
  int32_t request_id;
  int32_t response_to;
  int32_t op_code;
} MessageHeader;

typedef struct {
  int32_t flags;
  const char* full_collection_name;
  size_t full_collection_name_len;
  int32_t number_to_skip;
  int32_t number_to_return;
  const uint8_t* query;
  size_t query_len;
  const uint8_t* return_fields_selector;
  size_t return_fields_selector_len;
  bool has_return_fields_selector;
} OpQueryPayload;

typedef struct {
  int32_t response_flags;
  int64_t cursor_id;
  int32_t starting_from;
  int32_t number_returned;
  const uint8_t* documents_buf;
  size_t documents_buf_len;
} OpReplyPayload;

typedef enum {
  SECTION_KIND_0 = 0,
  SECTION_KIND_1 = 1
} SectionKind;

typedef struct {
  uint8_t* document;
  size_t document_len;
} OpMsgPayloadSection_Kind0;

typedef struct {
  int32_t size;
  char* document_sequence_identifier;
  size_t document_sequence_identifier_len;
  uint8_t* documents_buf;
  size_t documents_buf_len;
} OpMsgPayloadSection_Kind1;

typedef struct {
  SectionKind section_kind;
  union {
    OpMsgPayloadSection_Kind0 kind0_section;
    OpMsgPayloadSection_Kind1 kind1_section;
  } section;
} OpMsgPayloadSection;
 
typedef struct {
  uint32_t flag_bits;
  OpMsgPayloadSection* sections;
  size_t sections_len;
  uint32_t checksum;
  bool has_checksum;
} OpMsgPayload;

typedef enum {
  OP_QUERY = 2004,
  OP_REPLY = 1,
  OP_MSG = 2013
} OpCode;

typedef struct {
  MessageHeader header;
  OpCode op_code;
  union {
    OpQueryPayload query;
    OpReplyPayload reply;
    OpMsgPayload msg;
  } payload;
} WireMessage;

typedef enum {
  WIRE_DESERIALIZE_SUCCESS = 0,
  WIRE_INVALID_BUFFER = 1,
  WIRE_MISSING_BYTES = 2,
  WIRE_INVALID_OPCODE = 3,
} WireDeserializeError;

typedef enum {
  OP_MSG_DESERIALIZE_SUCCESS = 0,
  OP_MSG_INCOMPLETE_PAYLOAD = 1,
  OP_MSG_INVALID_PAYLOAD,
} OpMsgDeserializeError;

typedef enum {
  OP_MSG_SECTION_DESERIALIZE_SUCCESS = 0,
  OP_MSG_SECTION_INCOMPLETE_SECTION = 1,
  OP_MSG_SECTION_MALLOC_ERROR,
  OP_MSG_SECTION_INVALID_SECTION_KIND,
} OpMsgSectionDeserializeError;

WireMessage *wire_message_new_from_buf(const uint8_t *message_buf, size_t message_buf_len, WireDeserializeError *out_error);

#endif

