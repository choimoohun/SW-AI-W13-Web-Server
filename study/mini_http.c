#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>   // struct timeval 용

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);

    // 1) bind / listen 실패 여부 확인 추가
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind 실패 (이미 8080 포트가 사용 중일 수 있음)");
        return 1;
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen 실패");
        return 1;
    }
    printf("서버 실행 중: http://127.0.0.1:8080\n");

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;
        printf("server_fd = %d, client_fd = %d\n", server_fd, client_fd);
        // 2) 빈 연결에서 영원히 멈추지 않도록 read 타임아웃 2초 설정
        struct timeval tv = {2, 0};
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        char req_buf[4096] = {0};
        ssize_t n = read(client_fd, req_buf, sizeof(req_buf) - 1);

        if (n > 0) {
            char method[16] = {0}, path[256] = {0};
            sscanf(req_buf, "%15s %255s", method, path);
            printf("요청 들어옴 -> 메서드: %s, 경로: %s\n", method, path);

            char res_buf[2048];
            int res_len = 0;

            if (strcmp(path, "/favicon.ico") == 0) {
                res_len = snprintf(res_buf, sizeof(res_buf),
                    "HTTP/1.1 404 Not Found\r\n"
                    "Content-Length: 0\r\n"
                    "Connection: close\r\n"
                    "\r\n");
            }
            else if (strcmp(path, "/api") == 0) {
                const char *json = "{\"hello\":\"world\",\"port\":8080}";
                res_len = snprintf(res_buf, sizeof(res_buf),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json; charset=utf-8\r\n"
                    "Content-Length: %zu\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "%s",
                    strlen(json), json);
            }
            else {
                const char *html = "<h1>Hello from C Socket!</h1><a href='/api'>/api 가보기</a>";
                res_len = snprintf(res_buf, sizeof(res_buf),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html; charset=utf-8\r\n"
                    "Content-Length: %zu\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "%s",
                    strlen(html), html);
            }

            write(client_fd, res_buf, res_len);
        }

        close(client_fd);
    }

    close(server_fd);
    return 0;
}