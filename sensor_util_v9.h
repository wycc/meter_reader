#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <modbus-rtu.h>
#include <stdint.h>
#include <math.h>

#define WATER_METER_SERVER_ID 1
#define WATER_METER_ADDRESS   0x0304
#define WATER_METER_NB        0x4
#define DECIMAL_DIGIT_BIT_FOR_WATER  4
#define READ_BTYE_NUMBER   8

typedef struct{
    modbus_t *ctx;
    int server_id;
    int address;
    int number_of_bytes;
    int rc;
    struct timeval old_response;
    struct timeval new_response;
    uint32_t old_response_to_sec;
    uint32_t old_response_to_usec;
    uint32_t new_response_to_sec;
    uint32_t new_response_to_usec;
    uint32_t old_byte_to_sec;
    uint32_t old_byte_to_usec;
}Sensor;

int sensor_init(Sensor* sensor_ctx, int server_id, int address, int nb);
void convert_hex(uint8_t decimal_val, uint8_t* ten_digits, uint8_t* digits);
double deconde_hex_reading(uint8_t* reading);
void get_sensor_data(Sensor* sensor_ctx, uint8_t* tab_rp_bits);
// void close_sensor(Meter **meters, int meter_len);
