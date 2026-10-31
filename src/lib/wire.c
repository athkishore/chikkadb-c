#include<stdint.h>
#include<string.h>
#include<stdlib.h>
#include<stdio.h>

#include<wire.h>

OpMsgSectionDeserializeError decode_op_msg_sections(const uint8_t *cursor, size_t sections_len, WireMessage *wire_msg) {
  size_t remaining = sections_len;

  OpMsgSectionDeserializeError err = OP_MSG_SECTION_DESERIALIZE_SUCCESS;
  int section_count = 0;

  // First pass to determine the number of sections
  while (!err && remaining > 0) {
    uint8_t section_kind;
    memcpy(&section_kind, cursor, 1);
    cursor += 1;
    remaining -= 1;

    if (remaining < 4) {
      err = OP_MSG_SECTION_INCOMPLETE_SECTION;
      break;
    }
    uint32_t size;
    memcpy(&size, cursor, 4);

    if (size > remaining) {
      err = OP_MSG_SECTION_INCOMPLETE_SECTION;
      break;
    }

    cursor += size;
    remaining -= size;

    section_count++;
  }

  if (!err) {
    cursor -= sections_len;
    remaining = sections_len;
  }

  if (section_count == 0) {
    err = OP_MSG_SECTION_INCOMPLETE_SECTION;
    return err;
  }

  OpMsgPayloadSection *sections = malloc(section_count * sizeof(OpMsgPayloadSection));
  if (!sections) {
    err = OP_MSG_SECTION_MALLOC_ERROR;
    return err;
  }
  
  // Second pass to perform the deserialization
  int section_index = 0;
  while (!err && section_index < section_count) {
    uint8_t section_kind;
    memcpy(&section_kind, cursor, 1);
    cursor += 1;
    remaining -= 1;

    switch (section_kind) {
      case SECTION_KIND_0: {
        if (remaining < 4) {
          err = OP_MSG_SECTION_INCOMPLETE_SECTION;
          break;
        }
        uint32_t size;
        memcpy(&size, cursor, 4);

        if (size > remaining) {
          err = OP_MSG_SECTION_INCOMPLETE_SECTION;
          break;
        }

        uint8_t *document = malloc((size_t)size);
        if (!document) {
          err = OP_MSG_SECTION_MALLOC_ERROR;
          break;
        }

        memcpy(document, cursor, size);
        cursor += size;
        remaining -= size;

        (sections + section_index)->section_kind = SECTION_KIND_0;
        (sections + section_index)->section.kind0_section.document = document;
        (sections + section_index)->section.kind0_section.document_len = size;

        break;
      }

      case SECTION_KIND_1: {
        if (remaining < 4) {
          err = OP_MSG_SECTION_INCOMPLETE_SECTION;
          break;
        }
        uint32_t section_size;
        memcpy(&section_size, cursor, 4);
        cursor += 4;
        remaining -= 4;

        if (section_size - 4 > remaining) {
          err = OP_MSG_SECTION_INCOMPLETE_SECTION;
          break;
        }

        size_t section_remaining = section_size - 4;

        size_t document_sequence_identifier_len = strnlen((char *)cursor, section_remaining);
        if (document_sequence_identifier_len >= section_remaining) {
          err = OP_MSG_SECTION_INCOMPLETE_SECTION;
          break;
        }

        char *document_sequence_identifier = malloc(sizeof(char) * (document_sequence_identifier_len + 1));
        if (!document_sequence_identifier) {
          err = OP_MSG_SECTION_MALLOC_ERROR;
          break;
        }
        memcpy(document_sequence_identifier, cursor, document_sequence_identifier_len + 1);
        cursor += document_sequence_identifier_len + 1;
        remaining -= document_sequence_identifier_len + 1;
        section_remaining -= document_sequence_identifier_len + 1;

        size_t documents_buf_len = section_remaining;
        uint8_t *documents_buf = malloc(documents_buf_len);
        if (!documents_buf) {
          free(document_sequence_identifier);
          err = OP_MSG_SECTION_MALLOC_ERROR;
          break;
        }
        memcpy(documents_buf, cursor, documents_buf_len);
        cursor += documents_buf_len;
        remaining -= documents_buf_len;

        (sections + section_index)->section_kind = SECTION_KIND_1;
        (sections + section_index)->section.kind1_section.size = section_size;
        (sections + section_index)->section.kind1_section.document_sequence_identifier
          = document_sequence_identifier;

        (sections + section_index)->section.kind1_section.document_sequence_identifier_len
          = document_sequence_identifier_len;

        (sections + section_index)->section.kind1_section.documents_buf = documents_buf;
        (sections + section_index)->section.kind1_section.documents_buf_len = documents_buf_len;
        break;
      }

      default:
        err = OP_MSG_SECTION_INVALID_SECTION_KIND;
        break;
    }

    if (!err) section_index++;
  }

  if (!err) {
    wire_msg->payload.msg.sections = sections;
  } else {
    for (int i = 0; i < section_index; i++) {
      if ((sections + i)->section_kind == SECTION_KIND_0) {
        free((sections + i)->section.kind0_section.document);
      }

      if ((sections + i)->section_kind == SECTION_KIND_1) {
        free((sections + i)->section.kind1_section.document_sequence_identifier);
        free((sections + i)->section.kind1_section.documents_buf);
      }
    }
    free(sections);
  }

  return err;
}

OpMsgDeserializeError decode_op_msg_payload(const uint8_t *cursor, size_t payload_len, WireMessage *wire_msg) {
  if (payload_len < 10) {
    return OP_MSG_INCOMPLETE_PAYLOAD;
  }
  
  uint32_t flag_bits;
  memcpy(&flag_bits, cursor, 4);
  cursor += 4;
  wire_msg->payload.msg.flag_bits = flag_bits + 5;

  OpMsgSectionDeserializeError rv = decode_op_msg_sections(cursor, payload_len - 4, wire_msg);
  if (rv) {
    return OP_MSG_INVALID_PAYLOAD;
  }

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
      if (rv) {
        // TODO: Streamline error conversion
        *out_error = WIRE_INVALID_BUFFER;
      }
      
      fprintf(stdout, "OP_MSG\n");
      break;

    default:
      free(wire_msg);
      *out_error = WIRE_INVALID_OPCODE;
      return NULL;
  }

  return wire_msg;
}

// void wire_message_free(WireMessage *message) {

// }