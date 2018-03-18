#include <fcntl.h>

#define BINARY 2
#define OCTAL 10
#define HEX 16

#define DATA_PUBLISH_INTERVAL 10 * 1000 * 1000 // usec
#define MAX_HTTP_LOCK_SEC     5

#define MAX_BYTE_PER_LINE     150
#define DATA_FILE_SIZE_MB 1
#define TOTAL_LINES_OF_FILE ((DATA_FILE_SIZE_MB * 1024 * 1024 - 1) / MAX_BYTE_PER_LINE)
#define FILE_SIZE  TOTAL_LINES_OF_FILE * MAX_BYTE_PER_LINE

#define HEAD 0
#define TAIL 1

// extern FILE* data_file;

typedef struct{
    char station[30];
    int building;
    char floor[10];
    char house_number[10];
    char room[10];
    char type[30];
    char hostname[30];
    char http_port[10];
    char tcp_port[10];
    int address;
}Meter;

Meter** parse_config_file(char const* const config_file_name, int* meter_len);
char** str_split(char* a_str, const char a_delim);
void handle_if_data_queue_full(char const* const data_file_name, int64_t* jump_to_line, int meter_len);
void get_head_tail_in_full_data_file(FILE* data_file, int64_t* head_line, int64_t* tail_line, int64_t* total_lines_of_data);
