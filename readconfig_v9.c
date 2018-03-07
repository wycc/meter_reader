#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jsmn/jsmn.h"
#include <time.h>
#include "readconfig_v9.h"

#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>


static int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
    if (tok->type == JSMN_STRING && (int) strlen(s) == tok->end - tok->start &&
            strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
        return 0;
    }
    return -1;
}

Meter** parse_config_file(char const* const config_file_name, int *meter_len)
{
    // how can i load a whole file into string in C
    // https://stackoverflow.com/questions/7856741/how-can-i-load-a-whole-file-into-a-string-in-c
    // what is the difference in char const * and char * const and char const* const
    // https://stackoverflow.com/questions/890535/what-is-the-difference-between-char-const-and-const-char

    FILE* config_file = fopen(config_file_name, "r"); /* should check the result */
    fseek(config_file, 0, SEEK_END);
    long config_file_size = ftell(config_file);
    rewind(config_file);
    char *file_contents = malloc((config_file_size) * (sizeof(char)));
    size_t result = fread(file_contents, sizeof(char), config_file_size, config_file);
    if ((long)result != config_file_size) {
        printf("Reading error\n");
    }
    fgetc(config_file);
    if (feof(config_file)) {
        fclose(config_file);
        // printf("eof\n");
    } else {
        printf("not yet eof\n");
    }

    int r;
    jsmn_parser p;
    jsmntok_t t[1024]; /* We expect no more than 1024 tokens */

    jsmn_init(&p);
    r = jsmn_parse(&p, file_contents, strlen(file_contents), t, sizeof(t)/sizeof(t[0]));
    if (r < 0) {
        printf("Failed to parse JSON: %d\n", r);
        return (Meter**)1;
    }

    if (r < 1 || t[0].type != JSMN_ARRAY) {
        printf("Array expected\n");
        return (Meter**)1;
    }

    // printf("r:%d, t0:%d, t1:%d\n",r, t[0].size, t[1].size);

    // Passing dynamic array of struct to function for allocation
    // https://stackoverflow.com/questions/8460874/passing-dynamic-array-of-struct-to-function-for-allocation

    int array_size = t[0].size;
    *meter_len = array_size;
    Meter **array = malloc(sizeof(Meter *) * array_size);
    int token_pos = 1;
    int i;
    for (i=0; i<array_size; i++) {
        if (t[token_pos].type != JSMN_OBJECT) {
            printf("Object expected\n");
            continue;
        }
        Meter *temp = malloc(sizeof(Meter));
        int dictionary_size = t[token_pos].size;
        token_pos++;
        char buffer [100];
        int j;
        for (j = 0; j < dictionary_size; j++) {
            if (jsoneq(file_contents, &t[token_pos], "Station") == 0) {
                token_pos++;
                sprintf(buffer, "%.*s", t[token_pos].end-t[token_pos].start, file_contents + t[token_pos].start);
                // Split string with delimiters in C
                // https://stackoverflow.com/questions/9210528/split-string-with-delimiters-in-c
                char** tokens;
                tokens = str_split(buffer, '-');
                strcpy(temp->station, *(tokens));
                strcpy(temp->hostname, *(tokens+2));
                token_pos++;
            } else if (jsoneq(file_contents, &t[token_pos], "Room") == 0) {
                token_pos++;
                sprintf(buffer, "%.*s", t[token_pos].end-t[token_pos].start, file_contents + t[token_pos].start);
                char** tokens;
                strcpy(temp->room, buffer);
                tokens = str_split(buffer, '-');
                strcpy(temp->floor, *(tokens+1));
                strcpy(temp->house_number, *(tokens+2));
                token_pos++;
            } else if (jsoneq(file_contents, &t[token_pos], "Type") == 0) {
                token_pos++;
                sprintf(buffer, "%.*s", t[token_pos].end-t[token_pos].start, file_contents + t[token_pos].start);
                strcpy(temp->type, buffer);
                token_pos++;
            } else if (jsoneq(file_contents, &t[token_pos], "HTTP_Port") == 0) {
                token_pos++;
                sprintf(buffer, "%.*s", t[token_pos].end-t[token_pos].start, file_contents + t[token_pos].start);
                strcpy(temp->http_port, buffer);
                token_pos++;
            } else if (jsoneq(file_contents, &t[token_pos], "TCP_Port") == 0) {
                token_pos++;
                sprintf(buffer, "%.*s", t[token_pos].end-t[token_pos].start, file_contents + t[token_pos].start);
                strcpy(temp->tcp_port, buffer);
                token_pos++;
            } else if (jsoneq(file_contents, &t[token_pos], "Address") == 0) {
                // hex string to int
                // http://www.cplusplus.com/reference/cstdlib/strtol/
                token_pos++;
                sprintf(buffer, "%.*s", t[token_pos].end-t[token_pos].start, file_contents + t[token_pos].start);
                temp->address = strtol(buffer,NULL,HEX);
                token_pos++;
            } else {
                printf("tok len: %d\n",t[token_pos].end-t[token_pos].start);
                printf("token0: %c\n", *(file_contents+t[token_pos].start));
                printf("token1: %c\n", *(file_contents+t[token_pos].start+1));
                printf("token2: %c\n", *(file_contents+t[token_pos].start+2));
                printf("token3: %c\n", *(file_contents+t[token_pos].start+3));
                printf("Unexpected token_pos:%d\n",token_pos);
                printf("Unexpected key: %.*s\n", t[token_pos].end-t[token_pos].start, file_contents + t[token_pos].start);
                token_pos += 2;
            }
        }
        if (strcmp(temp->type,"energy_meter") == 0) {
            continue;
        } else if (strcmp(temp->type, "water_meter") == 0) {
            if (sensor_init(&temp->sensor_ctx, temp->address, WATER_METER_ADDRESS, WATER_METER_NB) == -1) {
                printf ("Error sensor init");
                exit(EXIT_FAILURE);
            }
        } else {
            continue;
        }

        *(array+i) = temp;

    }
    return array;
}

char** str_split(char* a_str, const char a_delim)
{
    char** result    = 0;
    size_t count     = 0;
    char* tmp        = a_str;
    char* last_comma = 0;
    char delim[2];
    delim[0] = a_delim;
    delim[1] = 0;

    /* Count how many elements will be extracted. */
    while (*tmp)
    {
        if (a_delim == *tmp)
        {
            count++;
            last_comma = tmp;
        }
        tmp++;
    }

    /* Add space for trailing token. */
    count += last_comma < (a_str + strlen(a_str) - 1);

    /* Add space for terminating null string so caller
       knows where the list of returned strings ends. */
    count++;

    result = malloc(sizeof(char*) * count);

    if (result)
    {
        size_t idx  = 0;
        char* token = strtok(a_str, delim);

        while (token)
        {
            //assert(idx < count);
            *(result + idx++) = strdup(token);
            token = strtok(0, delim);
        }
        //assert(idx == count - 1);
        *(result + idx) = 0;
    }

    return result;
}

void get_head_tail_in_full_data_file(FILE* data_file, int64_t* head_line, int64_t* tail_line, int64_t* total_lines_of_data) {
    rewind(data_file);
    double pre_line_timestamp = 0.0;
    int64_t i;
    for (i=0; i<TOTAL_LINES_OF_FILE; i++)
    {
        char temp_line_buffer[MAX_BYTE_PER_LINE-1] = {'\0'};
        int64_t j = 0;
        while(1)
        {
            char temp_c = fgetc(data_file);
            if (temp_c != '\n')
            {
                temp_line_buffer[j] = temp_c;
                j++;
            } else {
                break;
            }
        }

        int token_pos = 0;
        int r;
        jsmn_parser p;
        jsmntok_t t[1024];
        jsmn_init(&p);
        r = jsmn_parse(&p, temp_line_buffer,MAX_BYTE_PER_LINE-1, t, sizeof(t)/sizeof(t[0]));
        if (r < 0) {
            printf("[error] Failed to parse JSON: %d\n", r);
            return;
        }
        if (r < 1 || t[token_pos].type != JSMN_OBJECT) {
            printf("[error] Object expected\n");
            return;
        }
        int dictionary_size = t[token_pos].size;
        token_pos++;

        char dictionary_buffer[MAX_BYTE_PER_LINE];
        double this_line_timestamp;
        int k;
        for (k = 0; k < dictionary_size; k++) {
            if (jsoneq(temp_line_buffer, &t[token_pos], "Timestamp") == 0) {
                token_pos++;
                sprintf(dictionary_buffer, "%.*s", t[token_pos].end-t[token_pos].start, temp_line_buffer + t[token_pos].start);
                char *ptr;
                this_line_timestamp = strtod(dictionary_buffer, &ptr);
                token_pos++;
            } else {
                token_pos += 2;
            }
        }

        if (this_line_timestamp < pre_line_timestamp) {
            break;
        } else {
            pre_line_timestamp = this_line_timestamp;
        }
    }

    if (i < TOTAL_LINES_OF_FILE) {
        *head_line = i;
        *tail_line = i;
    } else {
        *head_line = 0;
        *tail_line = 0;
    }

    *total_lines_of_data = TOTAL_LINES_OF_FILE;
}

void close_sensor(Meter **meters, int meter_len){
  int i;
  for(i=0;i<meter_len;i++){
    modbus_close(meters[i]->sensor_ctx.ctx);
    modbus_free(meters[i]->sensor_ctx.ctx);
  }
}
