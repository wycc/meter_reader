#include "sensor_util_v9.h"

int sensor_init(Sensor* sensor_ctx, int server_id, int address, int nb) {
  // UPDATE THE DEVICE NAME AS NECESSARY
  sensor_ctx->server_id = server_id;
  sensor_ctx->address = address;
  sensor_ctx->number_of_bytes = nb;
  sensor_ctx->ctx = modbus_new_rtu("/dev/ttyUSB0", 9600, 'N', 8, 1);
  if (sensor_ctx->ctx == NULL) {
      fprintf(stderr, "Could not connect to MODBUS: %s\n", modbus_strerror(errno));
      return -1;
  }
  printf("Setting slave_id %d\n", sensor_ctx->server_id);
  fflush(stdout);
  sensor_ctx->rc = modbus_set_slave(sensor_ctx->ctx, sensor_ctx->server_id);

  if (sensor_ctx->rc == -1) {
      fprintf(stderr, "server_id=%d Invalid slave ID: %s\n", sensor_ctx->server_id, modbus_strerror(errno));
      modbus_free(sensor_ctx->ctx);
      return -1;
  }
  modbus_set_debug(sensor_ctx->ctx, TRUE);

  modbus_set_error_recovery(sensor_ctx->ctx,
                            MODBUS_ERROR_RECOVERY_LINK |
                            MODBUS_ERROR_RECOVERY_PROTOCOL);

  modbus_get_response_timeout(sensor_ctx->ctx, &sensor_ctx->old_response);
  if (modbus_connect(sensor_ctx->ctx) == -1) {
      fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
      modbus_free(sensor_ctx->ctx);
      return -1;
  }
  modbus_get_response_timeout(sensor_ctx->ctx, &sensor_ctx->new_response);
  return 0;
}

void convert_hex(uint8_t decimal_val, uint8_t* ten_digits, uint8_t* digits) {
  *ten_digits = decimal_val/16;
  *digits = decimal_val%16;
}

double deconde_hex_reading(uint8_t* reading){
  int i;
  uint8_t digits_after_decimal_point = 0;
  double degree = 0.0;
  uint8_t digits[READ_BTYE_NUMBER*2] = {0};
  // for(i=0;i<READ_BTYE_NUMBER;i++){
  //   printf("byte[%d]: %x\n",i,reading[i]);
  // }

  for(i=0;i<(READ_BTYE_NUMBER/2);i++){
    convert_hex(reading[i*2+1], &digits[i*4], &digits[i*4+1]);
    convert_hex(reading[i*2], &digits[i*4+2], &digits[i*4+3]);
  }

  for(i=0;i<(READ_BTYE_NUMBER*2-DECIMAL_DIGIT_BIT_FOR_WATER);i++){
    degree = degree * 10 + digits[i];
    // printf("digit:%d\n",digits[i]);
  }

  // printf("digits[READ_BTYE_NUMBER*2-2]:%d\n",digits[READ_BTYE_NUMBER*2-2]);
  // printf("digits[READ_BTYE_NUMBER*2-1]:%d\n",digits[READ_BTYE_NUMBER*2-1]);
  // for(i=DECIMAL_DIGIT_BIT_FOR_WATER;i>0;i--){
  //   digits_after_decimal_point = digits[READ_BTYE_NUMBER*2-i]*10 + digits_after_decimal_point;
  // }
  digits_after_decimal_point = digits[READ_BTYE_NUMBER*2-4]*1000 + digits[READ_BTYE_NUMBER*2-3]*100 + digits[READ_BTYE_NUMBER*2-2]*10+digits[READ_BTYE_NUMBER*2-1];
  degree /= pow(10.0,digits_after_decimal_point);
  return degree;
}

void get_sensor_data(Sensor* sensor_ctx, uint8_t* tab_rp_bits){
  sensor_ctx->rc = modbus_read_registers(sensor_ctx->ctx, sensor_ctx->address, sensor_ctx->number_of_bytes, (uint16_t *)tab_rp_bits);
  if (sensor_ctx->rc == -1) {
      fprintf(stderr, "Failed to modbus_read_input_registers: %s\n", modbus_strerror(errno));
      /* modbus_free(ctx);
      return -1; */
  }

}

