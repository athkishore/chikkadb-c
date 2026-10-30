#include<stdint.h>
#include<string.h>
#include<stdlib.h>
#include<stdio.h>

#include<wire.h>

OpMsgSectionDeserializeError decode_op_msg_sections(const int8_t *cursor, size_t sections_len, WireMessage *wire_msg) {
  size_t remaining = sections_len;

  while (remaining > 0) {
    uint8_t section_kind;
    memcpy(&section_kind, cursor, 1);
    cursor += 1;
    remaining -= 1;

    switch (section_kind) {
      case 0: {
        if (remaining < 5) {
          return OP_MSG_SECTION_INCOMPLETE_SECTION;
        }
        int32_t size;
        memcpy(&size, cursor, 4);
        cursor += 4;
        remaining -= 4;

        if (size > remaining) {
          return OP_MSG_SECTION_INCOMPLETE_SECTION;
        }

        uint8_t *document = malloc((size_t)size);
        if (!document) {
          return OP_MSG_SECTION_MALLOC_ERROR;
        }

        memcpy(document, cursor, size);

        OpMsgPayloadSection *section = malloc(sizeof(OpMsgPayloadSection));
        section->section_kind = SECTION_KIND_0;
        section->section.kind0_section.document = document;
        section->section.kind0_section.document_len = size;
      }
    }
  }
}

OpMsgDeserializeError decode_op_msg_payload(const int8_t *cursor, size_t payload_len, WireMessage *wire_msg) {
  if (payload_len < 10) {
    return OP_MSG_INCOMPLETE_PAYLOAD;
  }
  
  uint32_t flag_bits;
  memcpy(&flag_bits, cursor, 4);
  cursor += 4;
  wire_msg->payload.msg.flag_bits = flag_bits + 5;

  OpMsgSectionDeserializeError rv = decode_op_msg_sections(cursor, payload_len - 4, wire_msg);

  return OP_MSG_DESERIALIZE_SUCCESS;
}

WireMessage *wire_message_new_from_buf(const uint8_t *message_buf, size_t message_buf_len, WireDeserializeError *out_error) {
  WireDeserializeError dummy_error;
  if (out_error == NULL) {
    out_error = &dummy_error;
  }
  
  WireMessage *wire_msg = malloc(sizeof(WireMessage));
  if (!wire_msg) {
    *out_error = WIRE_INVALID_BUFFER;
    return NULL;
  }

  if (message_buf_len < 16) {
    *out_error = WIRE_MISSING_BYTES;
    return NULL;
  }

  const uint8_t *cursor = message_buf;
  memcpy(&wire_msg->header.message_length, cursor, 4);
  cursor += 4;

  memcpy(&wire_msg->header.request_id, cursor, 4);
  cursor += 4;

  memcpy(&wire_msg->header.response_to, cursor, 4);
  cursor += 4;

  uint32_t op_code;
  memcpy(&op_code, cursor, 4);
  cursor += 4;

  wire_msg->header.op_code = op_code;
  switch ((OpCode) op_code) {
    case OP_QUERY:
      fprintf(stdout, "OP_QUERY\n");
      break;

    case OP_REPLY:
      fprintf(stdout, "OP_REPLY\n");
      break;

    case OP_MSG:
      OpMsgDeserializeError rv = decode_op_msg_payload(
        cursor, message_buf_len - 16, wire_msg
      );
      fprintf(stdout, "OP_MSG\n");
      break;

    default:
      free(wire_msg);
      *out_error = WIRE_INVALID_OPCODE;
      return NULL;
  }

  return wire_msg;
}

void wire_message_free(WireMessage *message) {

}