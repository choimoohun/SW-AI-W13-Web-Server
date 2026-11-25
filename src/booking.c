#include "booking.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char train_id[] = "KTX101";

static struct seat {
    const char *id;
    bool reserved;
} seats[] = {
    {"1A", false}, {"1B", true}, {"1C", false},
    {"2A", false}, {"2B", false}, {"2C", true},
};

struct json_input {
    const char *next;
    const char *end;
};

static void skip_space(struct json_input *input)
{
    while (input->next < input->end && *input->next != '\0' &&
           strchr(" \t\n\r", *input->next) != NULL) {
        input->next++;
    }
}

static bool consume(struct json_input *input, char expected)
{
    skip_space(input);
    if (input->next == input->end || *input->next != expected) return false;
    input->next++;
    return true;
}

// 이 API의 필드명과 ID는 1~31자의 ASCII 식별자다.
static bool read_id(struct json_input *input, char *output, size_t capacity)
{
    if (!consume(input, '"')) return false;
    size_t length = 0;
    while (input->next < input->end && *input->next != '"') {
        char byte = *input->next++;
        if (!((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
              (byte >= '0' && byte <= '9') || byte == '_' || byte == '-') ||
            length == capacity - 1) {
            return false;
        }
        output[length++] = byte;
    }
    if (input->next == input->end || length == 0) return false;
    input->next++;
    output[length] = '\0';
    return true;
}

static bool parse_reservation(const char *body, size_t length, char train[32], char seat[32])
{
    struct json_input input = {body, body + length};
    if (!consume(&input, '{')) return false;

    for (size_t i = 0; i < 2; i++) {
        char key[32];
        if (!read_id(&input, key, sizeof key) || !consume(&input, ':')) return false;
        if (strcmp(key, "trainId") == 0 && *train == '\0') {
            if (!read_id(&input, train, 32)) return false;
        } else if (strcmp(key, "seatId") == 0 && *seat == '\0') {
            if (!read_id(&input, seat, 32)) return false;
        } else {
            return false;
        }
        if (!consume(&input, i == 0 ? ',' : '}')) return false;
    }

    skip_space(&input);
    return input.next == input.end;
}

static void reply_error(struct booking_response *response, int status, const char *error)
{
    response->status = status;
    snprintf(response->body, sizeof response->body, "{\"error\":\"%s\"}", error);
}

static void list_seats(struct booking_response *response)
{
    static const char item_format[] = "%s{\"id\":\"%s\",\"available\":%s}";
    int prefix = snprintf(response->body, sizeof response->body,
                          "{\"trainId\":\"%s\",\"seats\":[", train_id);
    if (prefix < 0 || (size_t)prefix >= sizeof response->body) {
        reply_error(response, 500, "response_too_large");
        return;
    }
    size_t used = (size_t)prefix;

    for (size_t i = 0; i < sizeof seats / sizeof seats[0]; i++) {
        size_t remaining = sizeof response->body - used;
        int count = snprintf(response->body + used, remaining, item_format,
                             i == 0 ? "" : ",", seats[i].id,
                             seats[i].reserved ? "false" : "true");
        if (count < 0 || (size_t)count >= remaining) {
            reply_error(response, 500, "response_too_large");
            return;
        }
        used += (size_t)count;
    }
    if (sizeof response->body - used < sizeof "]}") {
        reply_error(response, 500, "response_too_large");
        return;
    }
    memcpy(response->body + used, "]}", sizeof "]}");
    response->status = 200;
}

static bool is_json(const char *content_type)
{
    static const char json[] = "application/json";
    if (content_type == NULL ||
        strncasecmp(content_type, json, sizeof json - 1) != 0) return false;
    content_type += sizeof json - 1;
    content_type += strspn(content_type, " \t");
    return *content_type == '\0' || *content_type == ';';
}

static void reserve_seat(const char *content_type, const char *body, size_t length,
                         struct booking_response *response)
{
    char train[32] = "";
    char id[32] = "";
    if (!is_json(content_type)) {
        reply_error(response, 415, "unsupported_media_type");
        return;
    }
    if (!parse_reservation(body, length, train, id)) {
        reply_error(response, 400, "invalid_reservation");
        return;
    }
    if (strcmp(train, train_id) != 0) {
        reply_error(response, 404, "train_not_found");
        return;
    }

    for (size_t i = 0; i < sizeof seats / sizeof seats[0]; i++) {
        if (strcmp(seats[i].id, id) != 0) continue;
        // 단일 백엔드 루프가 빈자리 확인과 변경을 연달아 수행한다.
        if (seats[i].reserved) {
            reply_error(response, 409, "seat_already_reserved");
            return;
        }
        seats[i].reserved = true;
        response->status = 201;
        snprintf(response->body, sizeof response->body,
                 "{\"trainId\":\"%s\",\"seatId\":\"%s\",\"status\":\"reserved\"}",
                 train_id, seats[i].id);
        return;
    }
    reply_error(response, 404, "seat_not_found");
}

void booking_request(const char *method, const char *path, const char *content_type,
                     const char *body, size_t length, struct booking_response *response)
{
    response->allow = NULL;
    if (strcmp(path, "/api/seats") == 0) {
        if (strcmp(method, "GET") == 0) {
            list_seats(response);
        } else {
            response->allow = "GET";
            reply_error(response, 405, "method_not_allowed");
        }
    } else if (strcmp(path, "/api/reservations") == 0) {
        if (strcmp(method, "POST") == 0) {
            reserve_seat(content_type, body, length, response);
        } else {
            response->allow = "POST";
            reply_error(response, 405, "method_not_allowed");
        }
    } else {
        reply_error(response, 404, "route_not_found");
    }
}
