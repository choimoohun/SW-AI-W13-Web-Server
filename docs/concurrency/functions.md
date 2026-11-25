# 동시 요청 처리 함수 명세

[design.md](./design.md)의 `conn_queue.c`와 `httpd.c`에 들어가는 함수의 역할, 입력, 출력입니다.

## conn_queue.c

대기열은 `fds[head]`부터 `count`개가 들어 있는 원형 버퍼입니다. 모든 함수는 `lock`을 잡고 상태를 바꿉니다.

| 함수 | 설명 | 출력 |
| --- | --- | --- |
| `int conn_queue_init(conn_queue *q)` | 빈 대기열로 만들고 뮤텍스와 조건 변수를 초기화합니다. | 성공 0, 초기화 실패 -1(이미 만든 것은 정리) |
| `void conn_queue_push(conn_queue *q, int fd)` | 가득 차 있으면 `not_full`에서 기다린 뒤 끝에 넣고 `not_empty`로 워커 하나를 깨웁니다. 닫힌 뒤에는 부르지 않습니다. | 없음 |
| `int conn_queue_pop(conn_queue *q)` | 비어 있고 닫히지 않았으면 `not_empty`에서 기다립니다. 남은 것이 있으면 앞에서 꺼내고 `not_full`로 main을 깨웁니다. | 꺼낸 fd. 닫혔고 비었으면 -1 |
| `void conn_queue_close(conn_queue *q)` | `closed = 1`로 두고 `not_empty`를 broadcast해 잠든 워커를 모두 깨웁니다. | 없음 |
| `void conn_queue_destroy(conn_queue *q)` | 조건 변수와 뮤텍스를 정리합니다. 남은 fd는 닫지 않습니다. | 없음 |

깨어난 스레드는 조건을 `while`로 다시 확인합니다. 깨어났다고 일감이나 자리가 있다는 보장은 없기 때문입니다(가짜 깨어남, 다른 워커가 먼저 가져감).

## httpd.c

| 함수 | 설명 | 출력 |
| --- | --- | --- |
| `int main(int argc, char **argv)` | 신호 등록, 리스닝 소켓, 대기열, 워커 8개를 만든 뒤 `stop`이 설 때까지 `accept()`한 연결에 `FD_CLOEXEC`와 시간 제한을 붙여 대기열에 넣습니다. 그 뒤 [안전한 종료](./design.md#안전한-종료) 순서로 끝냅니다. | `argc < 3`(`<port>`나 `<root>`가 없음)이거나 `--route` 규칙이 잘못되면 원인 출력 후 `exit(1)`(7단계). 신호로 끝나면 0 |
| `static void *worker(void *arg)` | 대기열에서 연결을 꺼내 `handle_connection()` 후 닫기를 반복합니다. | `conn_queue_pop()`이 -1이면 `NULL`을 반환하고 끝남 |
| `static void handle_connection(int connfd)` | 4단계와 같고, 본문을 읽은 뒤 경로 조각 검사를 거쳐 [리버스 프록시](../reverse-proxy/design.md#1-전체-요청-흐름과-컴포넌트-구조)(7단계)와 [요청 분기](../dynamic-request/design.md#요청-분기)(CGI·정적)로 가릅니다. 정적 루트는 전역 `root`에서 읽습니다. | 없음. `connfd`는 닫지 않음 |
| `static void on_stop(int sig)` | `stop = 1`만 합니다. | 없음 |
