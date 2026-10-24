#include<stdint.h>
#include<string.h>
#include<stdlib.h>

#include<wire.h>



// TODO: Enumerate error codes
WireMessage *wire_message_new_from_buf(const uint8_t *message_buf, size_t message_buf_len, DeserializeError *out_error) {
  DeserializeError dummy_error;
  if (out_error == NULL) {
    out_error = &dummy_error;
  }
  
  WireMessage *wire_msg = malloc(sizeof(WireMessage));
  if (!wire_msg) {
    *out_error = DESERIALIZE_ERROR;
    return NULL;
  }

  if (message_buf_len < 16) {
    *out_error = DESERIALIZE_ERROR;
    return NULL;
  }

  const uint8_t *cursor = message_buf;
  memcpy(&wire_msg->header.message_length, cursor, 4);
  cursor += 4;

  memcpy(&wire_msg->header.request_id, cursor, 4);
  cursor += 4;

  memcpy(&wire_msg->header.response_to, cursor, 4);
  cursor += 4;

  memcpy(&wire_msg->header.op_code, cursor, 4);
  cursor += 4;

  return wire_msg;
}

void wire_message_free(WireMessage *message) {

}