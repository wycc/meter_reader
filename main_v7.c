// Copyright (c) 2014 Cesanta Software Limited
// All rights reserved
//
// This software is dual-licensed: you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation. For the terms of this
// license, see <http://www.gnu.org/licenses/>.
//
// You are free to use this software under the terms of the GNU General
// Public License, but WITHOUT ANY WARRANTY; without even the implied
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// Alternatively, you can license this software under a commercial
// license, as set out in <https://www.cesanta.com/license>.
//
// $Date: 2014-09-28 05:04:41 UTC $

#include "mongoose.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
// #include <pthread.h>
// https://gist.github.com/rtv/4989304
// https://docs.oracle.com/cd/E19455-01/806-5257/sync-24727/index.html
#include "readconfig_v7.h"

static sig_atomic_t s_received_signal = 0;

struct mg_mgr mgr;

FILE* config_file;
FILE* data_file;
char const* const config_file_name = "config_ldc.txt";
char const* const data_file_name = "data.txt";

Meter **meters;
int meter_len;
int has_config = 0;
int check_config_ok = 0;
int has_storage_full = 0;
int64_t head_line = 0;
int64_t tail_line = 0;
int64_t total_lines_of_data = 0;
static const char *s_http_port = "8002";
static const char *s_tcp_port = "7002";

struct timeval start, stop;

typedef struct{
  int lock;
  struct timeval lockTime;
}CV; // condition variable

CV http_lock, worker_lock;

static void signal_handler(int sig_num) {
  signal(sig_num, signal_handler);
  s_received_signal = sig_num;
  fclose(data_file);
  mg_mgr_free(&mgr);
}

void allocate_file_size()
{
  int i=0;
  char buf[MAX_BYTE_PER_LINE];

  data_file = fopen(data_file_name, "w");
  memset(buf,0,sizeof(buf));
  buf[MAX_BYTE_PER_LINE-1] = '\n';
  for(i=0;i<TOTAL_LINES_OF_FILE;i++) {
    fwrite(buf,1,MAX_BYTE_PER_LINE,data_file);
  }
  fclose(data_file);
}

int get_head_tail_in_data_file(int64_t *head_line, int64_t *tail_line)
{
  int line_count = 0;
  int zero_line_count = 0;
  char c;
  int position_in_file = 0;
  int find_head = 0;
  int find_tail = 0;
  int find_first = HEAD;
  int ret = -1;
  while( (line_count+1) * MAX_BYTE_PER_LINE <= FILE_SIZE ) {
    rewind(data_file);
    position_in_file = ftell(data_file);
    position_in_file += line_count * MAX_BYTE_PER_LINE;
    fseek(data_file, position_in_file, SEEK_SET);
    c = fgetc(data_file);

    if( c == '\0' ) {
      if (line_count == 0) {
        find_first = TAIL;
      }
      zero_line_count++;
    }

    if ( find_first == HEAD ) {
      if( c == '\0' ) {
        if ( !find_head ) {
          *head_line = line_count;
          find_head = 1;
        }
      } else {
        if ( find_head && !find_tail ) {
          *tail_line = line_count;
          find_tail = 1;
          ret = 1;
        }
      }
    } else if ( find_first == TAIL ) {
      if( c != '\0' ) {
        if ( !find_tail ) {
          *tail_line = line_count;
          find_tail = 1;
        }
      } else if ( c == '\0' ) {
        if ( find_tail && !find_head ) {
          *head_line = line_count;
          find_head = 1;
          ret = 1;
        }
      }
    }

    line_count++;
  }
  printf("line_count:%d\n", line_count);

  if ( find_first == HEAD ) {
    if (find_head && !find_tail) {
      *tail_line = 0;
      ret = 1;
    }
  } else if ( find_first == TAIL ) {
    if (find_tail && !find_head) {
      *head_line = 0;
      ret = 1;
    }
  }

  if (zero_line_count == TOTAL_LINES_OF_FILE) {
    *head_line = 0;
    *tail_line = 0;
    ret = 1;
  }

  total_lines_of_data = TOTAL_LINES_OF_FILE - zero_line_count;

  printf("ret:%d\n", ret);
  return ret;
}

float read_energy_meter(int address) {
    (void) address;
    uint16_t r = abs(rand());
    return ((r+10)%100)/10.0;
}

float read_water_meter(int address) {
    (void) address;
    uint16_t r = abs(rand());
    return (r%10)/10.0;
}

float get_meter_value(Meter *meter) {
    if (strcmp(meter->type,"energy_meter") == 0) {
        return read_energy_meter(meter->address);
    } else if (strcmp(meter->type, "water_meter") == 0) {
        return read_water_meter(meter->address);
    } else {
        return 0.0;
    }
}

static void ev_tcp_handler(struct mg_connection *nc, int ev, void *ev_data) {
  struct mbuf *io = &nc->recv_mbuf;
  (void) ev_data;
  char* buf = (char*)malloc(sizeof(char)*(io->len+1));

  switch (ev) {
    case MG_EV_ACCEPT:
      if (!worker_lock.lock) {
        has_config = 0;
      }
      break;
    case MG_EV_RECV:
      memcpy(buf, io->buf, io->len);
      buf[io->len] = '\0';
      if (!worker_lock.lock && has_config == 0){
        if (strcmp(buf, "config")==0) {
          check_config_ok = 1;
          if ( (config_file = fopen(config_file_name, "w+")) == NULL )
          {
            perror("[error] fail to open config file.");
          }
        } else if (strcmp(buf, "done")==0) {
          check_config_ok = 0;
          fclose(config_file);
          meters = parse_config_file(config_file_name, &meter_len);
          has_config = 1;
        } else {
          if (check_config_ok) {
            fprintf(config_file,"%s", buf);
          }
        }
      }
      mbuf_remove(io, io->len);       // Discard message from recv buffer
      break;
    case MG_EV_CLOSE:
      break;
    default:
      break;
  }
}

void clean_tail_line() {
  if ( tail_line <= (TOTAL_LINES_OF_FILE - 1) ) {
    rewind(data_file);
    int position_in_file = ftell(data_file);
    position_in_file += tail_line * MAX_BYTE_PER_LINE;
    fseek(data_file, position_in_file, SEEK_SET);
    fputc('\0', data_file);
  }

  if ( tail_line >= (TOTAL_LINES_OF_FILE - 1) ) {
    tail_line = 0;
  } else {
    tail_line++;
  }
  if (total_lines_of_data > 0) {
    total_lines_of_data--;
  }
}

void check_tail_line() {
  if (total_lines_of_data == TOTAL_LINES_OF_FILE) {
    tail_line = head_line;
  }
}

static void handle_http_request_get_line_number(struct mg_connection *nc, struct http_message *hm) {
  (void) hm;
  if (!worker_lock.lock) {
    mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    mg_printf_http_chunk(nc, "%d", total_lines_of_data);
    mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
  }
}

static void handle_http_request_scrape_data(struct mg_connection *nc, struct http_message *hm) {
  (void) hm;

  /* Get form variables */
  // char n1[100], n2[100];
  // double result;
  // mg_get_http_var(&hm->body, "n1", n1, sizeof(n1));
  // mg_get_http_var(&hm->body, "n2", n2, sizeof(n2));
  /* Compute the result and send it back as a JSON object */
  // result = strtod(n1, NULL) + strtod(n2, NULL);

  /* Send headers */
  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");

  if (total_lines_of_data > 0 && !worker_lock.lock) {
    check_tail_line();

    rewind(data_file);
    int position_in_file = ftell(data_file);
    position_in_file += tail_line * MAX_BYTE_PER_LINE;
    fseek(data_file, position_in_file, SEEK_SET);

    char buffer[MAX_BYTE_PER_LINE];
    fread(buffer,1,MAX_BYTE_PER_LINE,data_file);
    clean_tail_line();

    mg_printf_http_chunk(nc, "%s", buffer);
  }
  mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

static void handle_http_request_confirm(struct mg_connection *nc, struct http_message *hm) {
  (void) hm;
  http_lock.lock = 0;
  gettimeofday(&http_lock.lockTime,NULL);
  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

static void ev_restful_handler(struct mg_connection *nc, int ev, void *ev_data) {

  struct http_message *hm = (struct http_message *) ev_data;

  switch (ev) {
    case MG_EV_ACCEPT:
      if (!worker_lock.lock) {
        http_lock.lock = 1;
        printf("http post event accept\n");
        gettimeofday(&http_lock.lockTime,NULL);
      }
      break;
    case MG_EV_HTTP_REQUEST:
      if (mg_vcmp(&hm->uri, "/api/total_lines_of_data") == 0) {
        handle_http_request_get_line_number(nc, hm);
      } else if (mg_vcmp(&hm->uri, "/api/scrape") == 0) {
        handle_http_request_scrape_data(nc, hm); /* Handle RESTful call */
      } else if (mg_vcmp(&hm->uri, "/api/confirm") == 0) {
        handle_http_request_confirm(nc, hm);
      }
      break;
    case MG_EV_CLOSE:
      break;
    default:
      break;
  }
}

void *worker(void* param) {
  char *dummy = (char*) param;
  printf("[worker]%s\n",dummy);

  while (s_received_signal == 0) {
    gettimeofday(&stop,NULL);
    if (has_config && ((int)start.tv_sec+start.tv_usec/1000000.0) + DATA_PUBLISH_INTERVAL/1000000.0 < ((int)stop.tv_sec+stop.tv_usec/1000000.0)) {
      // gettimeofday(&begin,NULL);
      worker_lock.lock = 1;
      if (!http_lock.lock) {
        srand(time(NULL));
        int i;
        for (i=0; i<meter_len; i++) {
          gettimeofday(&start,NULL);
          Meter *meter = *(meters+i);
          float data = get_meter_value(meter);

          char buffer[MAX_BYTE_PER_LINE];
          memset(buffer, 0, sizeof(buffer));
          sprintf(buffer, "{ \"Room\":\"%s\", \"Type\":\"%s\", \"Data\":\"%f\", \"Timestamp\":\"%f\" }", meter->room, meter->type, data, (int)start.tv_sec+(float)start.tv_usec/1000000.0);
          int j;
          for (j=0;j<(MAX_BYTE_PER_LINE-1);j++)
          {
              if ( buffer[j] == '\0' )
              {
                  buffer[j] = ' ';
              }
          }
          buffer[MAX_BYTE_PER_LINE-1] = '\n';
          printf("buffer:%s", buffer);

          rewind(data_file);
          int position_in_file = ftell(data_file);
          if ( ((head_line+1) * MAX_BYTE_PER_LINE) <= FILE_SIZE ) {
            position_in_file += head_line * MAX_BYTE_PER_LINE;
          } else {
            head_line = 0;
          }
          printf("position_in_file:%d\n",position_in_file);
          fseek(data_file,position_in_file,SEEK_SET);
          fprintf(data_file,"%s", buffer);
          head_line++;
          if (total_lines_of_data < TOTAL_LINES_OF_FILE) {
            total_lines_of_data++;
          }
        }
      } else {
        if ( stop.tv_sec - http_lock.lockTime.tv_sec > MAX_HTTP_LOCK_SEC) {
          http_lock.lock = 0;
          gettimeofday(&http_lock.lockTime, NULL);
        }
      }
      worker_lock.lock = 0;
    }

    usleep(500*1000);
  }
  return NULL;
}


int main(void) {
  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);

  mg_mgr_init(&mgr, NULL);

  // --------------------------
  // worker thread for data collection
  // --------------------------
  if( access( config_file_name, F_OK ) != -1 ) {
    // file exists
    meters = parse_config_file(config_file_name, &meter_len);
    has_config = 1;
  }

  if( access( data_file_name, F_OK ) == -1 ) {
    allocate_file_size();
    data_file = fopen(data_file_name, "r, a+");
  } else {
    data_file = fopen(data_file_name, "r, a+");

    // head_line for write data, tail for read data
    if ( get_head_tail_in_data_file(&head_line, &tail_line) == -1 ) {
      get_head_tail_in_full_data_file(data_file, &head_line, &tail_line, &total_lines_of_data);
    }
    printf("total_lines_of_data:%lld\n", total_lines_of_data);
    printf("head_line:%lld, tail_line:%lld\n", head_line, tail_line);
  }
  worker_lock.lock = 0;
  gettimeofday(&worker_lock.lockTime,NULL);
  printf("worker lockTime:%d\n", (int)worker_lock.lockTime.tv_sec);

  mg_start_thread(worker, "");

  // ------------------------------------------
  // TCP server for updating configuration file
  // ------------------------------------------
  mg_bind(&mgr, s_tcp_port, ev_tcp_handler);
  printf("Starting echo mgr on ports %s\n", s_tcp_port);

  // ---------------------------------------------
  // restful server for handling HTTP POST request
  // ---------------------------------------------
  struct mg_connection *nc;
  struct mg_bind_opts bind_opts;
  const char *err_str;
  /* Set HTTP server options */
  memset(&bind_opts, 0, sizeof(bind_opts));
  bind_opts.error_string = &err_str;

  nc = mg_bind_opt(&mgr, s_http_port, ev_restful_handler, bind_opts);
  if (nc == NULL) {
    fprintf(stderr, "Error starting server on port %s: %s\n", s_http_port,
            *bind_opts.error_string);
    exit(EXIT_FAILURE);
  }
  mg_set_protocol_http_websocket(nc);
  printf("Starting RESTful server on port %s\n", s_http_port);

  gettimeofday(&start,NULL);
  http_lock.lock = 0;
  gettimeofday(&http_lock.lockTime,NULL);
  printf("http lockTime:%d\n", (int)http_lock.lockTime.tv_sec);

  while (s_received_signal == 0) {
    mg_mgr_poll(&mgr, 1000);
  }
  // mg_mgr_free(&mgr);

  return 0;
}
