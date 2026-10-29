#include "monitor_protocol.h"

static bool equal(const char *text, size_t length, const char *literal) {
    size_t i = 0;
    while (literal[i] != '\0') {
        if (i >= length || text[i] != literal[i]) return false;
        ++i;
    }
    return i == length;
}

static bool number(const char **cursor, const char *end, uint32_t *out) {
    uint32_t value = 0;
    const char *p = *cursor;
    if (p == end || *p < '0' || *p > '9') return false;
    while (p < end && *p >= '0' && *p <= '9') {
        unsigned digit = (unsigned)(*p++ - '0');
        if (value > (UINT32_MAX - digit) / 10U) return false;
        value = value * 10U + digit;
    }
    if (value == 0 || p == end || *p++ != ';') return false;
    *cursor = p;
    *out = value;
    return true;
}

const char *monitor_command_name(monitor_command_t command) {
    switch (command) {
        case MON_HELLO: return "HELLO";
        case MON_PING: return "PING";
        case MON_NAV: return "NAV";
        case MON_FS: return "FS";
        case MON_BEGIN: return "BEGIN";
        case MON_REPORT: return "REPORT";
    }
    return "INVALID";
}

bool monitor_parse_command(const char *text, size_t length, monitor_message_t *out) {
    if (length < 10 || length >= MONITOR_COMMAND_MAX || !equal(text, 4, "CMD;")) return false;
    monitor_message_t parsed = {0};
    const char *cursor = text + 4;
    const char *end = text + length;
    if (!number(&cursor, end, &parsed.session) ||
        !number(&cursor, end, &parsed.sequence)) return false;
    for (monitor_command_t cmd = MON_HELLO; cmd <= MON_REPORT; ++cmd) {
        const char *name = monitor_command_name(cmd);
        if (equal(cursor, (size_t)(end - cursor), name)) {
            parsed.command = cmd;
            *out = parsed;
            return true;
        }
    }
    return false;
}

int monitor_line_feed(monitor_line_t *line, char byte) {
    if (byte == '\n') {
        if (line->discard) {
            line->discard = false;
            line->used = 0;
            return -1;
        }
        line->data[line->used] = '\0';
        // used continua disponivel para o consumidor ate o proximo byte.
        return 1;
    }
    if (line->discard) return 0;
    if ((unsigned char)byte < 32 || (unsigned char)byte > 126 ||
        line->used >= sizeof(line->data) - 1U) {
        line->discard = true;
        line->used = 0;
        return 0;
    }
    line->data[line->used++] = byte;
    return 0;
}
