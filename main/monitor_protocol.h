#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MONITOR_COMMAND_MAX 96U
typedef enum { MON_HELLO, MON_PING, MON_NAV, MON_FS, MON_BEGIN, MON_REPORT } monitor_command_t;
typedef struct {
    uint32_t session;
    uint32_t sequence;
    monitor_command_t command;
} monitor_message_t;

// Texto estrito: CMD;session;sequence;HELLO|PING|NAV|FS|BEGIN|REPORT (sem newline).
bool monitor_parse_command(const char *text, size_t length, monitor_message_t *out);
const char *monitor_command_name(monitor_command_t command);

// Enquadramento TCP: aceita fragmentos e multiplas linhas. Linhas muito longas
// ou contendo bytes de controle sao descartadas INTEIRAS ate o proximo newline.
typedef struct {
    char data[MONITOR_COMMAND_MAX];
    size_t used;
    bool discard;
} monitor_line_t;
// 1: linha completa; 0: incompleta; -1: linha descartada.
int monitor_line_feed(monitor_line_t *line, char byte);
