# 13주차 학습 로드맵 31개 전체 영상 및 자료 1:1 상세 대본 집대성 보고서

본 보고서는 노트북에 등록된 30개 학습 자료(유튜브 강좌, 기술 웹 가이드)의 전체 대본과 강의 내용을 1:1 매칭하여 상세히 분석한 종합 서술 문서입니다. 각 자료별 구동 메커니즘, POSIX/리눅스 시스템 콜, C 언어 코드 구현 및 세부 대본 내용을 빠짐없이 다룹니다.

---

## 1. BJ.19 block I/O vs non-block I/O 설명합니다! 소켓 I/O 예제로 주로 설명해요. I/O multiplexing도 설명합니다
* **강의자 / 채널**: 쉬운코드
* **주요 주제**: 블로킹 I/O와 비블로킹 I/O의 커널 동작 방식, 소켓 버퍼 메커니즘, I/O 멀티플렉싱, 콜백/시그널 및 POSIX AIO/io_uring 개념

### 1) 상세 구동 메커니즘
* **I/O 및 소켓 버퍼**: 모든 네트워크 소켓에는 커널 메모리 상에 **송신 버퍼(Send Buffer)**와 **수신 버퍼(Receive Buffer)**가 각각 존재합니다.
* **블로킹 I/O (Blocking I/O)**:
  * 애플리케이션 스레드가 `read()` 시스템 콜을 호출했을 때, 수신 버퍼에 데이터가 없으면 데이터가 도달할 때까지 스레드가 Block 상태(대기)가 됩니다.
  * `write()` 시스템 콜 호출 시 송신 버퍼가 가득 차 있으면 여유 공간이 생길 때까지 스레드가 Block됩니다.
* **비블로킹 I/O (Non-Blocking I/O)**:
  * 시스템 콜 호출 시 데이터가 준비되지 않았더라도 스레드를 대기시키지 않고 즉시 반환합니다.
  * 리눅스 커널 기준 데이터 미준비 시 `-1`을 반환하며, `errno`로 `EWOULDBLOCK` 또는 `EAGAIN`을 설정합니다.
  * 단점: 데이터 준비 여부를 알기 위해 애플리케이션이 반복적으로 시스템 콜을 호출(Busy Polling)해야 하므로 CPU 자원이 낭비되고 완료 시점과의 타임 갭이 발생합니다.
* **I/O 멀티플렉싱 (I/O Multiplexing)**:
  * 단일 스레드가 `select()`, `poll()`, `epoll()` 시스템 콜을 통해 여러 소켓 FD를 동시에 커널에 감시 요청합니다.
  * 소켓 중 하나라도 읽기/쓰기 이벤트가 발생하면 블로킹이 해제되어 준비된 소켓만 순차적 또는 스레드 풀로 처리합니다.

### 2) C 언어 및 시스템 콜 동작 예시
```c
// 비블로킹 소켓 설정 예시 (fcntl)
int flags = fcntl(sockfd, F_GETFL, 0);
fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

// 비블로킹 read 호출 처리
ssize_t bytes_read = read(sockfd, buffer, sizeof(buffer));
if (bytes_read < 0) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
        // 데이터가 아직 준비되지 않음 - 다른 작업 수행 가능
    } else {
        // 실제 I/O 에러 처리
    }
}
```

### 3) 세부 대본 정리
강사는 백엔드 서버에서 소켓 통신 시 커널 모드로의 컨텍스트 스위칭과 유저 공간 데이터 복사 과정을 도식으로 설명합니다. IBM의 2006년 I/O 모델 분류 아티클에서 Async/Sync 및 Blocking/Non-blocking 매트릭스로 인해 발생한 혼선을 지적하며, I/O 멀티플렉싱은 개념적으로 비블로킹 I/O와 이벤트 감지가 결합된 구조임을 명확히 지적합니다. 또한 콜백/시그널 방식(POSIX AIO)과 최근 리눅스 커널의 고성능 비동기 I/O 프레임워크인 `io_uring`까지 소개합니다.

---

## 2. Beej's Guide to Network Programming & C Guides
* **작성자**: Brian "Beej Jorgensen" Hall
* **자료 형태**: 웹 기술 가이드 문서 (`https://beej.us/guide/`)

### 1) 상세 구동 메커니즘
* **POSIX 소켓 표준 가이드**: 인터넷 소켓 프로그래밍(`bgnet`), C 언어 문법 및 라이브러리 레퍼런스(`bgc`, `bgclr`), 유닉스 프로세스 간 통신(`bgipc`), Git 버전 관리(`bggit`) 등을 포함하는 입문서 및 레퍼런스 집합입니다.
* **IPC (Interprocess Communication)**: 소켓을 통한 프로세스 외부 통신 외에도, 단일 시스템 내에서 프로세스 간 데이터를 공유하는 공유 메모리(Shared Memory), 동기화를 위한 세마포어(Semaphore), 시그널(Signal), 메모리 맵 파일(mmap)의 메커니즘을 체계적으로 다룹니다.

### 2) 주요 C 소켓 프로그래밍 표준 흐름
```c
// getaddrinfo를 이용한 소켓 주소 구조체 설정 및 소켓 바인딩 표준 흐름
struct addrinfo hints, *res;
memset(&hints, 0, sizeof hints);
hints.ai_family = AF_UNSPEC;     // IPv4 또는 IPv6 자동 선택
hints.ai_socktype = SOCK_STREAM; // TCP 스트림 소켓
hints.ai_flags = AI_PASSIVE;     // 바인딩용 IP 자동 할당

getaddrinfo(NULL, "3490", &hints, &res);
int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
bind(sockfd, res->ai_addr, res->ai_addrlen);
listen(sockfd, 10);
freeaddrinfo(res);
```

### 3) 세부 내용 정리
Beej's Guide는 C 네트워크 프로그래머들에게 가장 보편적으로 인용되는 표준 가이드입니다. IPv4/IPv6 호환성을 위한 `getaddrinfo()` 사용법, `struct sockaddr_storage`를 통한 주소 독립적 프로그래밍, `select()` 및 `poll()`을 이용한 Multiplexing 구조, `sendall()` 헬퍼 함수 구현 등 실무에 즉시 적용 가능한 C 코드를 제공합니다.

---

## 3. Can I use connect with UDP sockets?
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: UDP 소켓에서의 `connect()` 시스템 콜 사용 가능 여부 및 의미 분석

### 1) 상세 구동 메커니즘
* **비연결형 UDP에서의 connect**: UDP(`SOCK_DGRAM`)는 핸드셰이크 과정이 없는 비연결형 프로토콜입니다. 하지만 UDP 소켓에 `connect()`를 호출하는 것이 기술적으로 가능합니다.
* **커널 레벨 동작**: UDP 소켓에서 `connect()`를 호출하면 실제 네트워크 패킷을 주고받는 핸드셰이크를 수행하는 것이 아니라, **커널 소켓 구조체에 기본 목적지 IP 주소와 포트 번호를 등록**합니다.
* **효과**:
  1. 이후 전송 시 목적지 주소를 매번 지정하는 `sendto()` 대신 `send()`나 `write()` 시스템 콜을 사용할 수 있습니다.
  2. 해당 소켓은 `connect()`로 지정된 목적지 주소로부터 들어오는 패킷만 수신하도록 필터링됩니다.

### 2) C 언어 구현 코드 비교
```c
// 기존 UDP 전송 방식 (connect 미사용)
struct sockaddr_in peer_addr;
// 주소 설정...
sendto(udp_fd, message, strlen(message), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));

// connect() 사용 UDP 전송 방식
connect(udp_fd, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
send(udp_fd, message, strlen(message), 0); // sendto 대신 send 사용 가능
```

### 3) 세부 대본 정리
Jacob Sorber는 전회 영상(UDP 패킷 송수신)에 달린 댓글 질문에 답하며 설명합니다. 수신자가 없는 상태에서 UDP 패킷을 전송하면 해당 패킷은 즉시 드롭(Drop)되며 재전송되지 않는 Best-effort 특징을 확인합니다. 또한 UDP 소켓에서 `connect()`를 호출하면 코드가 간결해지는 장점이 있으나, 코드 읽는 사람으로 하여금 TCP 연결 소켓으로 오인하게 만드는 혼선을 줄 수 있으므로 실무 적용 시 신중해야 함을 지적합니다.

---

## 4. Dealing with Endianness Issues in your Programs
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: 엔디안(Endianness), 빅 엔디안 vs 리틀 엔디안, 바이트 오더 변환 함수

### 1) 상세 구동 메커니즘
* **바이트 오더(Byte Order)**: 다중 바이트 정수(Short, Int, Long)가 메모리에 저장되는 순서입니다.
  * **빅 엔디안 (Big-Endian)**: 최상위 바이트(MSB)가 가장 낮은 메모리 주소에 저장됩니다. (모토로라, SPARC, 네트워크 표준)
  * **리틀 엔디안 (Little-Endian)**: 최하위 바이트(LSB)가 가장 낮은 메모리 주소에 저장됩니다. (Intel/AMD x86, ARM 호환)
* **네트워크 바이트 오더 (Network Byte Order)**: 이종 아키텍처 간 통신 오류를 방지하기 위해 인터넷 프로토콜 표준은 **빅 엔디안**을 네트워크 바이트 오더로 채택하고 있습니다.

### 2) C 언어 변환 함수
```c
#include <arpa/inet.h>

uint16_t host_port = 8080;
uint16_t net_port = htons(host_port); // Host to Network Short

uint32_t host_ip = INADDR_ANY;
uint32_t net_ip = htonl(host_ip);    // Host to Network Long

uint16_t recv_port = ntohs(net_port); // Network to Host Short
uint32_t recv_ip = ntohl(net_ip);     // Network to Host Long
```

### 3) 세부 대본 정리
Jacob Sorber는 그의 학생이 노트북(x86 리틀 엔디안)과 마이크로컨트롤러(Teensy 3.6) 간에 바이너리 정수 데이터를 전송하다가 숫자가 완전히 왜곡되는 버그를 겪은 사례로 강좌를 시작합니다. 텍스트 프로토콜(HTTP, JSON)은 변환이 필요 없지만 바이너리 프로토콜(DNS, 커스텀 소켓 프로토콜)에서는 `htons()`, `htonl()`, `ntohs()`, `ntohl()`을 반드시 사용해야 함을 강조합니다.

---

## 5. Detect Corruption with a Checksum
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: 체크섬(Checksum)을 통한 네트워크 데이터 손상 감지 원리 및 C 구현

### 1) 상세 구동 메커니즘
* **데이터 손상 감지**: 네트워크 전송 중 비트 반전(Bit Flip) 오작동으로 인한 데이터 파손을 감지하기 위해 데이터 패킷과 함께 작은 크기의 요약 값(Checksum)을 함께 전송합니다.
* **체크섬 알고리즘**:
  1. **Simple Addition Checksum**: 모든 바이트의 값을 합산하여 8비트 정수로 축약합니다.
  2. **XOR Checksum**: 모든 바이트를 XOR 연산하여 축약합니다.
* **한계 (Collision/상쇄 오류)**: 2개 이상의 비트가 동시에 반전되면 합산이나 XOR 값이 동일하게 유지되어 데이터 손상을 감지하지 못하는 충돌 문제가 발생하므로 실무에서는 CRC32나 MD5/SHA 해시를 사용합니다.

### 2) C 언어 구현 코드 분석
```c
// 비트 반전 함수 (테스트용)
char* corrupt_message(const char* message, int num_bits) {
    char* result = strdup(message);
    for (int i = 0; i < num_bits; i++) {
        int r_byte = rand() % strlen(message);
        int r_bit = rand() % 8;
        result[r_byte] ^= (1 << r_bit); // XOR 비트 반전
    }
    return result;
}

// 덧셈 기반 체크섬
unsigned char checksum_add(const char* msg, size_t len) {
    unsigned char sum = 0;
    for (size_t i = 0; i < len; i++) sum += msg[i];
    return sum;
}

// XOR 기반 체크섬
unsigned char checksum_xor(const char* msg, size_t len) {
    unsigned char x = 0;
    for (size_t i = 0; i < len; i++) x ^= msg[i];
    return x;
}
```

### 3) 세부 대본 정리
강사는 `rand()`와 비트 이동 연산자(`1 << r_bit`)를 이용해 의도적으로 문자열 비트를 조작하는 함수를 작성합니다. 2비트 이상 파손되었을 때 덧셈 체크섬과 XOR 체크섬이 손상을 놓치는 실제 충돌 사례를 시연하며 체크섬의 동작 원리와 한계를 설명합니다.

---

## 6. Easy Networking in C (libcurl)
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: libcurl 라이브러리를 활용한 C 네트워크 프로그래밍 및 콜백 함수 구현

### 1) 상세 구동 메커니즘
* **libcurl 하이레벨 라이브러리**: 소켓 API를 직접 제어하는 대신 HTTP/HTTPS, FTP, SMTP 등 다양한 네트워크 프로토콜을 손쉽게 수행할 수 있도록 지원합니다.
* **Easy API 패턴**:
  1. `curl_easy_init()`: 핸들 생성
  2. `curl_easy_setopt()`: 옵션 설정 (URL, 콜백 함수, 데이터 버퍼)
  3. `curl_easy_perform()`: 네트워크 요청 수행
  4. `curl_easy_cleanup()`: 자원 해제
* **콜백 함수(Callback Function)**: 네트워크 데이터 패킷이 수신될 때마다 사용자 지정 콜백 함수가 호출되어 수신 데이터를 처리합니다.

### 2) C 언어 구현 코드
```c
#include <curl/curl.h>

size_t got_data(void *buffer, size_t size, size_t nmemb, void *userp) {
    size_t total_bytes = size * nmemb;
    // 수신 데이터(buffer) 처리...
    return total_bytes; // 처리한 바이트 수 반환 (불일치 시 curl 중단)
}

int main() {
    CURL *curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://jacobsorber.com");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, got_data);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
}
```

### 3) 세부 대본 정리
Jacob Sorber는 low-level 소켓 프로그래밍과 달리 단 몇 줄의 코드로 HTTP 및 FTP 수신을 구현하는 실습을 진행합니다. cURL 수신 콜백 함수에서 수신 바이트 수와 일치하는 값을 반환하지 않으면 cURL이 전송을 중단한다는 주의점과, URL만 FTP 주소로 변경해도 동일한 코드 구조로 파일 다운로드가 동작함을 보여줍니다.

---

## 7. Highlighting a Student Programming Project (Code Review)
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: 학생 프로젝트(Dict.cc Curses 터미널 클라이언트) C 코드 리뷰, 모듈화, 보안 가이드

### 1) 상세 구동 메커니즘 및 캡슐화
* **터미널 Curses 사전 클라이언트**: 독일어-영어 번역 웹 사이트 Dict.cc의 HTTP 요청 및 정규표현식 파싱 결과를 Curses UI로 출력하는 C 프로젝트입니다.
* **모듈화 및 캡슐화**:
  * `.h` 파일에는 외부 공개용 인터페이스만 선언하고, 내부용 헬퍼 함수에는 `static` 키워드를 붙여 **소스 파일 스코프(Translation Unit)**로 제한합니다.
* **버퍼 오버플로우 방지**:
  * `strcpy()`, `sprintf()`는 버퍼 경계 검사를 하지 않아 보안 취약점(Buffer Overflow)을 유발합니다.
  * `strncpy()`, `snprintf()`를 사용하여 버퍼 크기를 명시적으로 제한해야 합니다.

### 2) C 코드 리팩토링 예시
```c
// 개선 전 (보안 위험)
strcpy(dest, src);
sprintf(buf, "GET /search?q=%s HTTP/1.1", query);

// 개선 후 (안전한 버퍼 처리 및 static 캡슐화)
static void internal_helper(void) { ... } // 외부 노출 제한

strncpy(dest, src, sizeof(dest) - 1);
dest[sizeof(dest) - 1] = ' ';
snprintf(buf, sizeof(buf), "GET /search?q=%s HTTP/1.1", query);
```

### 3) 세부 대본 정리
강사는 자신의 학생이자 후원자인 토마스(Tomas)의 GitHub 프로젝트를 분석하며 Make 파일 구성, 단위 테스트 분리, 정규표현식 및 cURL 활용 능력을 칭찬합니다. C 언어에서 객체지향의 `private` 멤버를 구현하기 위해 `static` 함수 및 변수를 활용하는 기법과 문자열 경계 검사의 중요성을 강조합니다.

---

## 8. How one thread listens to many sockets with select in C
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: C 언어로 작성하는 단일 스레드 기반 `select()` 소켓 I/O 멀티플렉싱 서버

### 1) 상세 구동 메커니즘
* **단일 스레드 동시성**: 스레드를 추가로 생성하지 않고, 단일 이벤트 루프 내에서 `select()`를 호출하여 여러 클라이언트 소켓의 읽기 가능 상태를 모니터링합니다.
* **`fd_set` 구조체 및 매크로**:
  * `FD_ZERO(&set)`: 비트셋 초기화
  * `FD_SET(fd, &set)`: 감시 대상 소켓 추가
  * `FD_CLR(fd, &set)`: 감시 대상 제거
  * `FD_ISSET(fd, &set)`: 해당 소켓에 읽기 데이터가 준비되었는지 확인
* **파괴적 비트셋 한계**: `select()` 호출 후 `fd_set`에는 준비된 소켓 비트만 남고 기존 감시 비트가 파괴되므로, 매 루프마다 원본 비트셋의 사본을 만들어 `select()`에 전달해야 합니다.

### 2) C 언어 구현 코드 구조
```c
fd_set current_sockets, ready_sockets;
FD_ZERO(&current_sockets);
FD_SET(server_fd, &current_sockets);
int max_fd = server_fd;

while (1) {
    ready_sockets = current_sockets; // 파괴적 변경 대비 사본 생성
    if (select(max_fd + 1, &ready_sockets, NULL, NULL, NULL) < 0) break;

    for (int i = 0; i <= max_fd; i++) {
        if (FD_ISSET(i, &ready_sockets)) {
            if (i == server_fd) {
                // 신규 클라이언트 접속 수락 (accept)
                int client_fd = accept(server_fd, NULL, NULL);
                FD_SET(client_fd, &current_sockets);
                if (client_fd > max_fd) max_fd = client_fd;
            } else {
                // 기존 클라이언트 데이터 수신 처리 (read)
                handle_client(i);
                // 연결 종료 시: FD_CLR(i, &current_sockets); close(i);
            }
        }
    }
}
```

### 3) 세부 대본 정리
Jacob Sorber는 멀티스레드 없이 단일 스레드로 동시 접속을 처리하는 `select()` 서버 코드를 단계별로 작성합니다. `select()`가 `FD_SETSIZE`(기본 1024) 제한을 가지며 $0$부터 `max_fd`까지 전체 루프를 돌아야 하는 $O(N)$ 성능 한계를 설명하고, 비트셋 복사 패턴을 강조합니다.

---

## 9. How to Send and Receive UDP packets (in C)
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: C 언어를 이용한 UDP 데이터그램 패킷 송신 및 수신 프로그램 구현

### 1) 상세 구동 메커니즘
* **비연결형 통신**: UDP 소켓(`SOCK_DGRAM`)은 3-way 핸드셰이크나 연결 확립 과정이 없습니다.
* **주소 지정 송수신**:
  * 송신 시 `sendto()` 함수에 목적지 IP와 포트 주소 구조체(`sockaddr_in`)를 전달합니다.
  * 수신 시 `recvfrom()` 함수를 호출하면 패킷 수신과 동시에 송신자의 IP/포트 주소 정보가 구조체에 채워집니다.

### 2) C 언어 송수신 코드 분석
```c
// 1) UDP 송신 (sendto)
int sock = socket(AF_INET, SOCK_DGRAM, 0);
struct sockaddr_in peer_addr;
peer_addr.sin_family = AF_INET;
peer_addr.sin_port = htons(9800);
inet_pton(AF_INET, "127.0.0.1", &peer_addr.sin_addr);

sendto(sock, message, strlen(message) + 1, 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));

// 2) UDP 수신 (bind & recvfrom)
int rx_sock = socket(AF_INET, SOCK_DGRAM, 0);
struct sockaddr_in my_addr, from_addr;
my_addr.sin_family = AF_INET;
my_addr.sin_port = htons(9800);
my_addr.sin_addr.s_addr = INADDR_ANY;

bind(rx_sock, (struct sockaddr*)&my_addr, sizeof(my_addr));

socklen_t addr_len = sizeof(from_addr);
ssize_t bytes = recvfrom(rx_sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&from_addr, &addr_len);
```

### 3) 세부 대본 정리
강사는 `UDP_send.c`와 `UDP_receive.c` 두 프로그램을 작성하여 로컬 루프백(`127.0.0.1`) 주소로 메시지를 송수신합니다. `recvfrom()`의 6번째 인자인 주소 길이 변수가 단순 값이 아닌 **In/Out 포인터(`socklen_t*`)** 형태로 전달되어 커널이 실제 주소 크기를 채워 넣는 구조임을 강조합니다.

---

## 10. How to build a web client? (sockets)
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: C 언어와 POSIX 소켓 API를 이용한 커스텀 HTTP 웹 클라이언트 구현

### 1) 상세 구동 메커니즘
* **웹 클라이언트 동작 과정**:
  1. `socket()`으로 스트림 소켓 생성.
  2. `inet_pton()`으로 문자열 IP를 바이너리 주소로 변환.
  3. `connect()`로 웹서버의 80번 포트에 접속 (TCP 3-way 핸드셰이크).
  4. `write()`로 HTTP GET 요청 프로토콜 문자열 전송.
  5. `read()` 루프를 통해 웹서버 응답 HTML 데이터를 수신하여 화면에 출력.

### 2) C 언어 구현 코드
```c
int sockfd = socket(AF_INET, SOCK_STREAM, 0);
struct sockaddr_in serv_addr;
serv_addr.sin_family = AF_INET;
serv_addr.sin_port = htons(80); // HTTP 표준 포트
inet_pton(AF_INET, "142.250.190.46", &serv_addr.sin_addr); // Google IP

connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

char *req = "GET / HTTP/1.1
Host: google.com
Connection: close

";
write(sockfd, req, strlen(req));

char buffer[4096];
ssize_t n;
while ((n = read(sockfd, buffer, sizeof(buffer) - 1)) > 0) {
    buffer[n] = ' ';
    printf("%s", buffer);
}
close(sockfd);
```

### 3) 세부 대본 정리
Jacob Sorber는 핑(ping) 명령어로 구글의 IP 주소를 알아낸 뒤, 웹 브라우저가 사용하는 HTTP 프로토콜의 가장 원초적인 GET 요청 문자열을 만들어 소켓에 전송하고 HTML 응답을 읽어들이는 클라이언트를 실습합니다.

---

## 11. How to get an IP address from a host name? (Example in C)
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: `getaddrinfo()` 함수를 활용한 도메인 이름(DNS) 주소 변환 구현

### 1) 상세 구동 메커니즘
* **DNS 룩업 표준 API (`getaddrinfo`)**: 하드코딩된 IP 대신 `google.com` 같은 도메인 이름을 입력받아 IP 주소 구조체 연결 리스트(`struct addrinfo`)로 변환해 줍니다.
* **`hints` 구조체**: 수신하고자 하는 주소 유형(AF_INET, AF_INET6, AF_UNSPEC)과 소켓 타입(SOCK_STREAM, SOCK_DGRAM)을 사전 필터링합니다.
* **메모리 해제 (`freeaddrinfo`)**: 동적으로 할당된 연결 리스트 전체를 해제하기 위해 반드시 `freeaddrinfo()`를 호출해야 합니다.

### 2) C 언어 구현 코드
```c
struct addrinfo hints, *result, *rp;
memset(&hints, 0, sizeof(hints));
hints.ai_family = AF_UNSPEC;     // IPv4 & IPv6 모두 허용
hints.ai_socktype = SOCK_STREAM; // TCP

if (getaddrinfo("google.com", "80", &hints, &result) != 0) {
    // 에러 처리
}

for (rp = result; rp != NULL; rp = rp->ai_next) {
    char addr_str[INET6_ADDRSTRLEN];
    void *addr;
    if (rp->ai_family == AF_INET) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)rp->ai_addr;
        addr = &(ipv4->sin_addr);
    } else {
        struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)rp->ai_addr;
        addr = &(ipv6->sin6_addr);
    }
    inet_ntop(rp->ai_family, addr, addr_str, sizeof(addr_str));
    printf("IP: %s
", addr_str);
}
freeaddrinfo(result);
```

### 3) 세부 대본 정리
강사는 도메인 이름을 IP로 변환하는 `getaddrinfo()`의 연결 리스트 탐색 과정을 보여주며, IPv4와 IPv6 포인터 캐스팅 방식 및 `inet_ntop()` 변환 기법을 상세히 설명합니다.

---

## 12. How to set a socket timeout (setsockopt) in C
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: `setsockopt()` 시스템 콜을 활용한 소켓 수신 타임아웃 설정

### 1) 상세 구동 메커니즘
* **블로킹 해제 메커니즘**: 소켓 `recvfrom()`이나 `read()`는 데이터가 수신될 때까지 무한정 대기합니다. `setsockopt()`로 `SO_RCVTIMEO` 옵션을 설정하면 지정된 시간이 지난 후 수신이 자동 중단됩니다.
* **타임아웃 감지**: 타임아웃 발생 시 함수는 `-1`을 반환하며 `errno`에 `EWOULDBLOCK` 또는 `EAGAIN`을 설정합니다.

### 2) C 언어 구현 코드
```c
struct timeval tv;
tv.tv_sec = 5;  // 5초 타임아웃
tv.tv_usec = 0;

if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    perror("setsockopt error");
}

ssize_t res = recvfrom(sockfd, buf, len, 0, NULL, NULL);
if (res < 0) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
        printf("Socket Timeout Occurred!
");
    } else {
        perror("Other Socket Error");
    }
}
```

### 3) 세부 대본 정리
Jacob Sorber는 이전 UDP 수신 코드에 5초 타임아웃을 적용하여 무한 블로킹을 방지하고 `EWOULDBLOCK` 에러 조건문을 통해 일반 에러와 타임아웃을 분기하는 실습을 진행합니다.

---

## 13. How to write a multithreaded server in C (threads, sockets)
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: 연결별 스레드 생성(`pthread_create`) 방식의 멀티스레드 소켓 서버 구현

### 1) 상세 구동 메커니즘
* **연결별 스레드 모델**: 메인 스레드는 `accept()` 루프만 실행하고, 연결 수락 시 클라이언트 FD를 힙에 할당하여 새 스레드(`pthread_create`)로 전달합니다.
* **장점**: 대용량 다운로드나 느린 클라이언트로 인해 서버 전체가 블로킹되는 현상을 방지합니다.
* **단점**: 동시 접속자 수가 수천 개로 폭증하면 스레드 생성 비용과 스택 메모리 소모로 인해 성능이 마비됩니다.

### 2) C 언어 구현 코드
```c
void* handle_connection(void* p_client_socket) {
    int client_socket = *((int*)p_client_socket);
    free(p_client_socket); // 힙 동적 메모리 해제
    
    // 파일 읽기 요청 처리...
    close(client_socket);
    return NULL;
}

int main() {
    // socket, bind, listen...
    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        int *pclient = malloc(sizeof(int));
        *pclient = client_fd;
        
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_connection, pclient);
        pthread_detach(thread_id); // 스레드 리소스 자동 회수
    }
}
```

### 3) 세부 대본 정리
강사는 동시 50개 요청 시 순차 처리 서버와 멀티스레드 서버의 처리 속도를 비교합니다. 느린 다운로드 요청이 섞여 있을 때 순차 처리는 모든 후속 요청을 지연시키지만, 멀티스레딩은 각 요청을 독립적으로 처리함을 보여줍니다.

---

## 14. How to write a multithreaded webserver using condition variables (Part 3)
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: 스레드 풀의 Busy Waiting 문제 해결을 위한 조건 변수(`pthread_cond_t`) 활용

### 1) 상세 구동 메커니즘
* **Busy Waiting 문제**: 스레드 풀의 일감 큐가 비어있을 때 스레드들이 무한 루프로 큐를 계속 확인하면 CPU 점유율이 100%까지 치솟습니다.
* **조건 변수 동기화 (`pthread_cond_t`)**:
  * 워커 스레드는 큐가 비어있을 때 `pthread_cond_wait(&cond, &mutex)`를 호출하여 대기(Sleep) 상태로 들어갑니다.
  * 메인 스레드가 큐에 새 작업 소켓을 수락해 넣을 때 `pthread_cond_signal(&cond)`을 호출하여 대기 중인 워커 스레드 하나를 깨웁니다.

### 2) C 언어 핵심 동기화 코드
```c
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

// 워커 스레드 함수
void* thread_function(void *arg) {
    while (1) {
        pthread_mutex_lock(&lock);
        node_t *node = dequeue();
        if (node == NULL) {
            pthread_cond_wait(&cond, &lock); // 원자적으로 뮤텍스 해제 및 대기
            node = dequeue();
        }
        pthread_mutex_unlock(&lock);
        
        if (node != NULL) {
            handle_connection(node->client_socket);
            free(node);
        }
    }
}

// 메인 스레드 접속 수락
int client_fd = accept(server_fd, NULL, NULL);
pthread_mutex_lock(&lock);
enqueue(client_fd);
pthread_cond_signal(&cond); // 대기 중인 스레드 신호
pthread_mutex_unlock(&lock);
```

### 3) 세부 대본 정리
Jacob Sorber는 `top` 명령어로 확인한 스레드 풀의 CPU 100% 점유 문제를 보여준 뒤, `pthread_cond_wait`와 `pthread_cond_signal`을 적용하여 CPU 점유율을 0%로 떨어뜨리는 개선 과정을 시연합니다.

---

## 15. I/O multiplexing: Doing I/O with many sources using select, poll and epoll calls in Linux
* **강의자 / 채널**: SoftPrayog
* **주요 주제**: 리눅스 환경에서의 `select()`, `poll()`, `epoll()` 시스템 콜 전체 비교 및 구현 상세

### 1) 상세 구동 메커니즘
* **`select()`**: `fd_set` 비트셋 모니터링. $O(N)$ 전체 순회 및 `FD_SETSIZE`(1024) 제한 존재.
* **`poll()`**: `struct pollfd` 배열 사용. 개수 제한이 없으나 유저-커널 공간 배열 복사 및 $O(N)$ 검사 오버헤드 존재.
* **`epoll()` (Linux 특화)**:
  * 커널 내에 Red-Black Tree와 레디 리스트(Ready List) 인스턴스 생성.
  * `epoll_ctl()`로 관심 FD 등록.
  * `epoll_wait()`는 이벤트가 실제로 발생한 소켓 FD 배열만 반환하므로 **$O(1)$ 최고 성능** 제공.

### 2) C 언어 epoll API 구현
```c
int epfd = epoll_create1(0);

struct epoll_event ev, events[MAX_EVENTS];
ev.events = EPOLLIN;
ev.data.fd = listen_sock;
epoll_ctl(epfd, EPOLL_CTL_ADD, listen_sock, &ev);

while (1) {
    int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
    for (int i = 0; i < nfds; i++) {
        if (events[i].data.fd == listen_sock) {
            int conn_sock = accept(listen_sock, NULL, NULL);
            ev.events = EPOLLIN;
            ev.data.fd = conn_sock;
            epoll_ctl(epfd, EPOLL_CTL_ADD, conn_sock, &ev);
        } else {
            // 기존 소켓 이벤트 처리 (recv)
        }
    }
}
```

### 3) 세부 대본 정리
SoftPrayog은 공항 항공편 시간 안내 서버(Flight Time Server) 예제를 바탕으로 `select`, `poll`, `epoll` 세 가지 방식으로 서버 코드를 차례로 리팩토링하며 메모리 복사량과 이벤트 탐색 알고리즘의 차이를 명쾌하게 비교합니다.

---

## 16. Making Minimalist Web Server in C on Linux
* **강의자 / 채널**: Nir Lichtman
* **주요 주제**: C 언어와 `sendfile()` 시스템 콜을 활용한 최소한의 고성능 리눅스 HTTP 웹서버 구현

### 1) 상세 구동 메커니즘
* **HTTP GET 파싱**: `recv()`로 읽어들인 `GET /index.html HTTP/1.1` 요청에서 5번째 바이트 위치를 잡고 첫 공백을 ` `으로 대치하여 파일 경로만 격리합니다.
* **`sendfile()` 제로 카피(Zero-Copy)**: 유저 공간 버퍼로 파일 데이터를 읽어 소켓으로 다시 쓰는 대신, 커널 디스크 버퍼에서 소켓 버퍼로 직접 전송하여 I/O 오버헤드를 극소화합니다.

### 2) C 언어 구현 코드
```c
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

int main() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { AF_INET, htons(8800), 0 };
    bind(s, (struct sockaddr*)&addr, sizeof(addr));
    listen(s, 10);

    int client_fd = accept(s, 0, 0);
    char buffer[256] = {0};
    recv(client_fd, buffer, 256, 0);

    // GET /file.html 문자열 파싱
    char *f = buffer + 5; // "GET /" 이후 문자열 위치
    *strchr(f, ' ') = ' '; // 공백 위치에 Null 문자 삽입

    int opened_fd = open(f, O_RDONLY);
    sendfile(client_fd, opened_fd, 0, 256); // Zero-copy 파일 전송

    close(opened_fd);
    close(client_fd);
    close(s);
}
```

### 3) 세부 대본 정리
Nir Lichtman은 단 30여 줄의 C 코드로 작성한 웹서버를 `strace` 도구로 추적하며 `bind`, `listen`, `accept`, `sendfile` 시스템 콜의 구동을 확인하고 `wget` 테스트로 정적 파일 전송을 검증합니다.

---

## 17. Multithreaded Server Part 2: Thread Pools
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: C 언어 스레드 풀(Thread Pool) 및 뮤텍스 동기화를 통한 소켓 서버 안정성 확보

### 1) 상세 구동 메커니즘
* **고정 크기 스레드 풀**: 미리 생성된 고정 개수(예: 20개)의 워커 스레드가 작업 큐(Queue)를 모니터링합니다.
* **경쟁 상태(Race Condition) 방지**: 여러 스레드가 동시에 큐에 접근하여 `enqueue`/`dequeue`할 때 발생하는 포인터 파손을 막기 위해 `pthread_mutex_t`로 큐 조작을 보호합니다.

### 2) C 언어 큐 동기화 코드
```c
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void enqueue_client(int client_socket) {
    pthread_mutex_lock(&lock);
    // 큐에 소켓 FD 노드 추가 작업...
    pthread_mutex_unlock(&lock);
}

int dequeue_client() {
    pthread_mutex_lock(&lock);
    // 큐에서 소켓 FD 노드 추출...
    pthread_mutex_unlock(&lock);
    return res_fd;
}
```

### 3) 세부 대본 정리
Jacob Sorber는 스레드 풀 없이 50개 접속을 일시에 쏟아부었을 때 발생하는 메모리 고갈 문제를 보여준 뒤, 스레드 풀과 뮤텍스 동기화를 적용하여 서버 크래시 없는 안정적인 응답 구조를 완성합니다.

---

## 18. Practical use case for fork and pipe in C
* **강의자 / 채널**: CodeVault
* **주요 주제**: `fork()`와 `pipe()`를 활용한 멀티프로세스 병렬 연산 및 IPC 구현

### 1) 상세 구동 메커니즘
* **`pipe(fd)`**: 단방향 데이터 통로를 생성하여 `fd[0]`(읽기 단)과 `fd[1]`(쓰기 단) 디스크립터를 반환합니다.
* **`fork()` 병렬 연산**: 자식 프로세스는 배열 전반부 합을 계산하여 파이프에 쓰고, 부모 프로세스는 배열 후반부 합을 계산한 뒤 파이프에서 자식의 결과를 읽어 합산합니다.

### 2) C 언어 구현 코드
```c
int fd[2];
pipe(fd);

int id = fork();
if (id == 0) { // 자식 프로세스
    close(fd[0]);
    int child_sum = compute_sum(arr, 0, mid);
    write(fd[1], &child_sum, sizeof(child_sum));
    close(fd[1]);
} else { // 부모 프로세스
    close(fd[1]);
    int parent_sum = compute_sum(arr, mid, end);
    int child_sum = 0;
    read(fd[0], &child_sum, sizeof(child_sum));
    close(fd[0]);
    wait(NULL); // 자식 프로세스 종료 대기
    int total = parent_sum + child_sum;
}
```

### 3) 세부 대본 정리
CodeVault 강사는 파이프 통신 시 불필요한 파이프 끝단(`close(fd[0])` / `close(fd[1])`)을 즉시 닫아주지 않으면 EOF 감지가 안 되어 읽기 작업이 영구 블로킹되는 실무 오류를 설명합니다. 이 구조가 CGI 구현의 기본 원리임을 안내합니다.

---

## 19 & 20. Program your own web server in C. (sockets)
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: POSIX 소켓 API를 이용한 커스텀 C 웹서버 구현 및 `common.h` 구조

### 1) 상세 구동 메커니즘
* **포트 바인딩 (18000 포트)**: 루트 권한 없이 실행 가능하도록 비특권 포트를 사용합니다.
* **HTTP 헤더 종단 감지**: 수신 데이터 버퍼에서 `

` (CRLF CRLF) 연속 기호를 감지하여 요청 끝을 판단합니다.
* **HTTP 200 OK 응답**: 수신 완료 후 `HTTP/1.0 200 OK

Hello` 상태 코드와 응답 바디를 소켓에 전송합니다.

### 2) C 언어 핵심 루프
```c
int listenfd = socket(AF_INET, SOCK_STREAM, 0);
struct sockaddr_in servaddr = { AF_INET, htons(18000), htonl(INADDR_ANY) };

bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
listen(listenfd, 10);

while (1) {
    int connfd = accept(listenfd, NULL, NULL);
    char buf[4096];
    read(connfd, buf, sizeof(buf)); // HTTP GET 요청 수신
    
    char *response = "HTTP/1.0 200 OK

Hello World!";
    write(connfd, response, strlen(response));
    close(connfd);
}
```

### 3) 세부 대본 정리
Jacob Sorber는 Chrome 브라우저로 `http://localhost:18000`에 접속하여 "Hello World" 문구가 뜨는 것을 확인하고, Chrome이 자동으로 보내는 파비콘(`GET /favicon.ico`) 추가 요청 로그를 함께 분석합니다.

---

## 21. Proxy vs Reverse Proxy Server Explained
* **강의자 / 채널**: Hussein Nasser
* **주요 주제**: 포워드 프록시와 리버스 프록시, L4/L7 프록시 및 사이드카(Sidecar) 아키텍처

### 1) 상세 구동 메커니즘
* **Forward Proxy**: 클라이언트 전단에 위치하여 대리 요청을 수행. 서버는 실제 클라이언트 IP를 알지 못함. (보안, 사이트 차단, 캐싱)
* **Reverse Proxy**: 백엔드 서버 전단에 위치하여 클라이언트 요청 수신. 클라이언트는 실제 백엔드 서버 IP를 알지 못함. (보안, 로드밸런싱, SSL Termination, 캐싱)
* **L4 vs L7**:
  * **L4**: IP/TCP 포트 수준 라우팅. 높은 처리 속도.
  * **L7**: HTTP URL/헤더/쿠키 분석 라우팅. 카나리 배포, 스마트 분산 가능.

### 2) 세부 대본 정리
Hussein Nasser는 포워드 프록시와 리버스 프록시의 "익명성의 방향"을 비교하여 설명하고, Envoy/Linkerd 같은 사이드카 프록시가 마이크로서비스 간 통신에서 gRPC/HTTP2 프로토콜 변환 및 타임아웃 처리를 전담하는 구조를 체계적으로 설명합니다.

---

## 22. Socket servers can get client addresses (accept, inet_ntop)
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: `accept()` 및 `inet_ntop()`을 활용한 클라이언트 IP 주소 추출

### 1) 상세 구동 메커니즘
* **클라이언트 주소 획득**: `accept(listenfd, (struct sockaddr*)&client_addr, &addr_len)` 호출 시 2번째, 3번째 인자로 주소 구조체 포인터와 길이를 넘겨 연결된 클라이언트 정보를 채웁니다.
* **`inet_ntop()` 변환**: 바이너리 형태 주소를 `inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str))`를 통해 점으로 구분된 십진수 IP 문자열로 변환합니다.

### 2) C 언어 구현 코드
```c
struct sockaddr_in client_addr;
socklen_t addr_len = sizeof(client_addr);
int connfd = accept(listenfd, (struct sockaddr*)&client_addr, &addr_len);

char ip_str[INET_ADDRSTRLEN];
inet_ntop(AF_INET, &(client_addr.sin_addr), ip_str, sizeof(ip_str));
printf("Client Connected from: %s:%d
", ip_str, ntohs(client_addr.sin_port));
```

### 3) 세부 대본 정리
Jacob Sorber는 `accept()`의 NULL 인자 대신 `sockaddr_in` 포인터를 채워 넣고 클라이언트 접속 로그를 출력하는 실습을 보여줍니다.

---

## 23. Sockets and Pipes Look Like Files (Unix/fdopen)
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: 유닉스 파일 추상화 철학 및 `fdopen()`을 이용한 소켓의 `FILE*` 변환

### 1) 상세 구동 메커니즘
* **유닉스 철학 ("Everything looks like a file")**: 파일, 파이프, 소켓은 모두 정수형 파일 디스크립터(FD)로 다뤄집니다.
* **`fdopen()` 변환**: 정수형 소켓 FD를 `FILE *fp = fdopen(sockfd, "r")`로 변환하면 `fgets()`, `fprintf()` 등 버퍼링 입출력 함수를 동일하게 적용할 수 있습니다.

### 2) C 언어 구현 코드
```c
int sockfd = connect_to_server("127.0.0.1", 80);
FILE *fp = fdopen(sockfd, "r"); // 소켓 FD를 FILE* 포인터로 변환

char line[1024];
while (fgets(line, sizeof(line), fp) != NULL) {
    printf("Line: %s", line);
}
fclose(fp); // 소켓 및 스트림 닫기
```

### 3) 세부 대본 정리
강사는 표준 입력(Standard In), 일반 파일, 소켓 세 가지 입출력 대상을 단 하나의 `fgets()` 읽기 루프 코드로 처리하도록 `fdopen()`을 적용하는 일관된 설계 기법을 소개합니다.

---

## 24. The Linux socket API explained
* **강의자 / 채널**: Chris Kanich
* **주요 주제**: 리눅스 커널 소켓 API의 전체 수명주기 및 개념적 비유

### 1) 상세 구동 메커니즘
* **Active Open (Client)**: `socket()` -> `connect()`
* **Passive Open (Server)**: `socket()` -> `bind()` -> `listen()` -> `accept()`
* **개념 비유**:
  * `socket()`: 문틀만 세우는 단계
  * `bind()`: 레스토랑 테이블 예약
  * `listen()`: 영업 개시 표지판 걸기
  * `accept()`: 문지기가 손님을 받아서 전담 안내원 소켓(`connfd`)에 연결하기

### 2) 세부 대본 정리
Chris Kanich 교수는 소켓 프로그래밍 각 시스템 콜의 커널 동작과 비유를 곁들여 설명하며, `send()` 호출 완료가 실제 네트워크 전달 완료를 의미하는 것이 아니라 커널 송신 버퍼 복사 완료만을 의미한다는 점을 강조합니다.

---

## 25. The two main types of network socket
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: 스트림 소켓(TCP)과 데이터그램 소켓(UDP)의 동작 원리 비교

### 1) 상세 구동 메커니즘
* **Stream Socket (`SOCK_STREAM` / TCP)**: 연속적인 바이트 흐름. 신뢰성 보장(손실 패킷 재전송, 순서 정렬, 혼잡 제어). 웹(HTTP), 파일 전송에 사용.
* **Datagram Socket (`SOCK_DGRAM` / UDP)**: 독립적 패킷 단위. 비신뢰성, 최선형 전송(Best-effort). 지연이 적어 화상 회의(Zoom), 실시간 스트리밍에 사용.

### 2) 세부 대본 정리
Jacob Sorber는 화상 통신 중 패킷 1개가 손실되었을 때 TCP처럼 재전송을 기다리며 화면을 멈추는 것보다 손실된 1개 프레임을 넘기고 다음 프레임을 즉시 재생하는 UDP 방식이 훨씬 뛰어난 사용자 경험을 제공함을 지적합니다.

---

## 26. Why are Progress Bars Always Wrong? Progress Bar Example in C.
* **강의자 / 채널**: Jacob Sorber
* **주요 주제**: C 언어 터미널 프로그레스 바 구현 및 `fflush(stdout)` 버퍼링 처리

### 1) 상세 구동 메커니즘
* **동일 라인 갱신**: 복귀 문자(`
`, Carriage Return)를 출력하여 커서를 줄의 맨 앞으로 이동시킨 뒤 새로운 문자열을 덮어씁니다.
* **버퍼 플러시 (`fflush`)**: C 표준 출력이 줄바꿈(`
`) 전까지 버퍼링되므로 매 프레임마다 `fflush(stdout)`를 호출해야 터미널에 즉시 반영됩니다.

### 2) C 언어 구현 코드
```c
void update_bar(int percent_done) {
    int num_chars = (percent_done * 30) / 100;
    printf("
["); // 
로 커서를 맨 앞으로 이동
    for (int i = 0; i < num_chars; i++) printf("#");
    for (int i = 0; i < 30 - num_chars; i++) printf(" ");
    printf("] %d%%", percent_done);
    fflush(stdout); // 표준 출력 버퍼 강제 비우기
}
```

### 3) 세부 대본 정리
Jacob Sorber는 네트워크 다운로드 크기의 불확실성으로 인해 프로그레스 바 예측이 틀릴 수밖에 없는 이유를 설명하며 터미널 상에서 실시간으로 늘어나는 프로그레스 바 코드를 실습합니다.

---

## 27. [10분 테코톡] 조앤의 Forward Proxy vs Reverse Proxy vs Load Balancer
* **강의자 / 채널**: 조앤 (우아한테크)
* **주요 주제**: 포워드 프록시, 리버스 프록시, 로드밸런서의 역할 및 계층별(L2~L7) 분산 방식

### 1) 상세 구동 메커니즘
* **Scale-up vs Scale-out**: 단일 서버 성능 향상(Scale-up)의 한계를 극복하기 위해 여러 대의 서버로 부하를 분산(Scale-out)합니다.
* **로드밸런서 계층**:
  * L2 (Mac 주소) / L3 (IP 주소) / L4 (TCP/UDP 포트) / L7 (HTTP URL/헤더/쿠키)
* **포워드 프록시 vs 리버스 프록시**:
  * 포워드 프록시: 클라이언트 사이드 위치, 부적절한 사이트 필터링, 보안, 캐싱.
  * 리버스 프록시: 서버 사이드 위치, 보안(서버 IP 은닉), DMZ 구성, 빠른 웹 캐시.

### 2) 세부 대본 정리
조앤 발표자는 초등학교 부적절 사이트 차단 사례로 포워드 프록시를, 웹서버 보안 및 캐싱 사례로 리버스 프록시를 비교하여 명쾌하게 설명합니다.

---

## 28. what's the difference between processes, threads, and io multiplexing?
* **강의자 / 채널**: Chris Kanich
* **주요 주제**: 멀티프로세스, 멀티스레드, I/O 멀티플렉싱 세 가지 동시성 모델의 근본적 차이점

### 1) 상세 구동 메커니즘
* **프로세스 (Process)**: 가상 주소 공간, 파일 디스크립터 표를 독립적으로 가짐. 안정적이지만 컨텍스트 스위칭 비용 높음.
* **스레드 (Thread)**: 동일 프로세스 내에서 가상 메모리와 FD 공유. 스택과 레지스터만 독립 유지. 스위칭이 가볍지만 뮤텍스 동기화 필수.
* **I/O 멀티플렉싱 (I/O Multiplexing)**: 단일 스레드 기반 이벤트 루프. OS의 선점형 스위칭 없이 명시적 I/O 이벤트 발생 시 상태 머신(State Machine) 전환으로 작업을 처리하여 최고의 확장성 제공.

### 2) 세부 대본 정리
Chris Kanich 교수는 이벤트 기반 멀티플렉싱이 프레임워크나 최신 언어의 `async`/`await` 키워드의 기초임을 설명하고, 단일 스레드 이벤트 루프가 스레드 생성이나 뮤텍스 동기화 오버헤드 없이 수만 개의 동시 연결을 다루는 메커니즘을 총정리합니다.

---


## 29. BJ.5 프로세스, 스레드, 멀티태스킹, 멀티스레딩, 멀티프로세싱, 멀티프로그래밍까지 한 방에 깔끔하게 설명합니다!

### 1) 상세 구동 메커니즘
* **운영체제의 핵심 실행 단위 정립**:
  * **프로그램 (Program)**: 저장 장치(Disk)에 바이너리 형태로 존재하는 비활성 상태의 코드 및 데이터 집합입니다.
  * **프로세스 (Process)**: 메모리에 로드되어 운영체제 커널로부터 독립된 가상 주소 공간(Code, Data, Heap, Stack)과 PCB(Process Control Block), 파일 디스크립터 테이블을 할당받아 실행 중인 프로그램의 인스턴스입니다.
  * **스레드 (Thread)**: 프로세스 내에서 실행되는 제어 흐름(Flow of Execution)의 최소 단위입니다. 각 스레드는 고유한 레지스터 세트와 스택(Stack) 영역만 보유하며, 속한 프로세스의 Code, Data, Heap 영역 및 파일 디스크립터를 다른 스레드들과 공유합니다.
* **다중 실행 패러다임 비교**:
  * **멀티프로그래밍 (Multiprogramming)**: 단일 CPU 환경에서 I/O 작업 등으로 대기 시간이 발생할 때 CPU가 놀지 않고 다른 프로그램으로 전환하여 CPU 이용률(Utilization)을 극대화하는 기법입니다.
  * **멀티태스킹 (Multitasking)**: 시분할(Time-sharing) 시스템을 기반으로 CPU 사용 시간을 매우 짧은 단위(Time Slice)로 쪼개어 여러 프로세스/스레드를 교대로 실행함으로써 사용자가 동시에 여러 작업을 수행하는 것처럼 느끼게 합니다.
  * **멀티스레딩 (Multithreading)**: 하나의 프로세스 안에서 복수의 스레드를 생성하여 논리적 동시성(Concurrency) 및 병렬성(Parallelism)을 달성하는 기법으로, 자원 공유가 용이하나 동기화 이슈가 발생합니다.
  * **멀티프로세싱 (Multiprocessing)**: 다중 CPU 또는 멀티 코어(Multi-Core) 물리 환경에서 여러 프로세스/스레드가 실제로 동시에(Parallel) 병렬 처리되는 상태입니다.
* **자원 공유와 동기화(Synchronization) 필요성**:
  * 프로세스 간에는 독립된 주소 공간을 가지므로 한 프로세스의 오류가 다른 프로세스에 영향을 주지 않지만, IPC(Inter-Process Communication: Pipe, Shared Memory, Socket 등)를 거쳐야만 통신이 가능합니다.
  * 스레드 간에는 Heap과 Data 영역을 공유하므로 통신 오버헤드가 적고 빠른 데이터 교환이 가능하지만, 동시 수정 시 경쟁 상태(Race Condition)가 발생하므로 뮤텍스(Mutex), 세마포어(Semaphore) 등의 동기화 메커니즘이 필수적입니다.

### 2) C 언어 및 운영체제 자원 구조 분석
* **프로세스 생성 (`fork`)과 스레드 생성 (`pthread_create`)의 자원 구조 차이**:
  * `fork()` 호출 시 커널은 부모 프로세스의 가상 메모리와 PCB를 복제(Copy-On-Write 방식)하여 독립된 자식 프로세스를 생성합니다.
  * `pthread_create()` 호출 시 커널은 동일한 프로세스 주소 공간 내에 새로운 TCB(Thread Control Block)와 전용 스택 영역만 할당합니다.
* **공유 자원 접근 및 동기화 코드 구조 예시**:
```c
#include <stdio.h>
#include <pthread.h>

int shared_counter = 0; // Data 영역 (모든 스레드가 공유)
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void* thread_func(void* arg) {
    for (int i = 0; i < 100000; i++) {
        pthread_mutex_lock(&lock);
        shared_counter++; // 임계 영역 (Critical Section)
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

int main() {
    pthread_t t1, t2;
    pthread_create(&t1, NULL, thread_func, NULL);
    pthread_create(&t2, NULL, thread_func, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    printf("Final Counter: %d
", shared_counter); // 200000 보장
    return 0;
}
```

### 3) 세부 대본 정리
* **프로세스와 스레드의 본질 해설**: 디스크의 파일(프로그램)이 메모리에 올려지는 순간 프로세스가 되며, 운영체제 스케줄러가 실제로 CPU 코어에 올리는 스케줄링 단위는 스레드입니다.
* **동시성(Concurrency)과 병렬성(Parallelism)의 차이**: 단일 코어에서 시분할로 빠르게 번갈아 실행되는 것은 동시성이며, 멀티 코어에서 실제로 여러 코어가 동시에 명령어를 수행하는 것은 병렬성입니다.
* **백엔드 서버 및 시스템 프로그래밍에서의 시사점**: 데이터 무결성을 유지하면서도 최대 성능을 내기 위해서는 프로세스 기반 아키텍처와 스레드 기반 아키텍처의 자원 격리 및 공유 특성을 정확히 이해하고 적절한 동기화 및 I/O 모델을 선택해야 합니다.

## 30. HTTP/1.1 vs HTTP/2 vs HTTP/3 Visually Explained | System Design Tutorial #7

### 1) 상세 구동 메커니즘
HTTP(HyperText Transfer Protocol)는 웹 브라우저와 웹 서버 간의 데이터 통신을 담당하는 응용 계층 프로토콜입니다. 성능 및 네트워크 효율성 극대화를 위해 HTTP/1.1부터 HTTP/2, HTTP/3에 이르기까지 전송 프로토콜과 다중화(Multiplexing) 메커니즘이 획기적으로 발전해 왔습니다.

1. **HTTP/1.1 (1997년)**:
   * **지속 연결(Keep-Alive)**: 매 요청마다 TCP 3-Way Handshake를 반복하던 HTTP/1.0의 단점을 극복하기 위해 단일 TCP 연결을 재사용하는  헤더를 도입했습니다.
   * **HTTP 파이프라이닝(Pipelining)**: 응답을 기다리지 않고 여러 HTTP 요청을 연속 전송하는 기능을 도입했습니다.
   * **한계 (HTTP 수준의 Head-of-Line Blocking)**: 응답은 반드시 요청이 보내진 순서대로 돌아와야 하므로, 앞선 요청(예: 대용량 이미지)의 처리가 지연되면 뒤따르는 모든 요청이 블로킹되는 **HTTP HOL Blocking** 문제가 발생합니다. 브라우저는 이를 회피하기 위해 도메인당 최대 6개의 TCP 연결을 생성하는 방식으로 임시 대처했습니다.

2. **HTTP/2 (2015년)**:
   * **바이너리 프레이밍 계층 (Binary Framing Layer)**: 텍스트 기반 메시지를  프레임과  프레임 등의 바이너리 단위로 세분화했습니다.
   * **멀티플렉싱 (Multiplexing)**: 단일 TCP 연결 내부에서 여러 개의 독립된 스트림(Stream ID)을 생성하고, 프레임들을 조각내어 교차 전송(Interleaving)함으로써 HTTP 수준의 HOL Blocking을 완전히 해결했습니다.
   * **헤더 압축 (HPACK)**: 중복 수신되는 HTTP 헤더를 Huffman 코딩 및 인덱스 테이블 기반으로 압축하여 전송 오버헤드를 대폭 줄였습니다.
   * **서버 푸시 (Server Push)**: 클라이언트가 요청하기 전에 CSS, JS 등 필요 자원을 서버가 선제적으로 전송합니다.
   * **한계 (TCP 수준의 Head-of-Line Blocking)**: 단일 TCP 연결을 공유하므로, 네트워크 패킷 1개가 손실되면 TCP 커널 버퍼가 해당 패킷의 재전송 및 정렬을 완료할 때까지 연결 내의 **모든 스트림이 함께 블로킹**되는 문제가 남았습니다.

3. **HTTP/3 (2022년)**:
   * **QUIC (Quick UDP Internet Connections) 도입**: 전송 계층 프로토콜을 TCP에서 **UDP**로 교체하고, 커널 상단에 QUIC 프로토콜을 도입했습니다.
   * **전송 계층 수준의 완전 멀티플렉싱**: QUIC은 각각의 스트림을 독자적인 패킷 흐름으로 다루므로, 특정 스트림의 패킷이 손실되어도 다른 스트림의 데이터 전송에는 **전혀 영향을 주지 않아 TCP HOL Blocking을 완벽 해소**했습니다.
   * **빠른 연결 수립 (1-RTT / 0-RTT)**: TCP Handshake와 TLS 1.3 암호화 Handshake를 단일 과정으로 통합하여 1-RTT(재접속 시 0-RTT)만에 보안 연결을 완료합니다.
   * **연결 마이그레이션 (Connection Migration)**: IP 주소와 포트 기반이 아닌 64비트 Connection ID로 식별하므로, 사용자가 Wi-Fi에서 LTE/5G 네트워크로 전환되어 IP가 바뀌어도 기존 연결이 끊기지 않고 유지됩니다.

---

### 2. 프로토콜별 특징 비교표

| 항목 | HTTP/1.1 | HTTP/2 | HTTP/3 |
| :--- | :--- | :--- | :--- |
| **전송 계층 프로토콜** | TCP | TCP | **UDP (QUIC)** |
| **데이터 형식** | 텍스트 (Plain Text) | 바이너리 프레임 (Binary Frame) | 바이너리 프레임 (Binary Frame) |
| **다중화 (Multiplexing)** | 순차 처리 (Pipelining 한계) | **단일 TCP 멀티플렉싱** | **스트림 독립형 UDP 멀티플렉싱** |
| **Head-of-Line Blocking** | HTTP 계층 HOL Blocking 발생 | **TCP 계층 HOL Blocking 존재** | **HOL Blocking 완전 해소** |
| **헤더 압축** | 미지원 | HPACK (Huffman + Index) | QPACK (out-of-order 호환) |
| **연결 수립 지연** | 2~3 RTT (TCP + TLS) | 2~3 RTT (TCP + TLS) | **1 RTT 이하 (0-RTT 지원)** |
| **네트워크 전환** | 연결 끊김 (IP 변경 시) | 연결 끊김 (IP 변경 시) | **Connection ID로 끊김 없음** |

---

### 3. 세부 내용 정리
* **HTTP/1.1의 병목 원인**: 텍스트 데이터의 비효율성, 오버헤드가 큰 요청/응답 헤더 중복 전송, 순차적 응답 대기로 인한 HTTP 수준 HOL Blocking이 주 원인입니다.
* **HTTP/2의 혁신과 한계**: 프레임 단위 분할 및 멀티플렉싱을 통해 하나의 TCP 연결로 효율성을 극대화했으나, 전송 계층이 여전히 TCP이므로 패킷 손실 시 전체 스트림 지연이 발생하는 한계를 가집니다.
* **HTTP/3 및 QUIC의 미래성**: UDP 위에 신뢰성과 암호화를 직접 탑재한 QUIC을 사용함으로써, 패킷 손실 시 해당 스트림만 영향을 받도록 격리하고 핸드셰이크 시간을 극적으로 단축시켰습니다.


## 31. Stop Confusing API Gateway, Load Balancer And Reverse Proxy (ByteByteGo)

### 1) 상세 구동 메커니즘
본 강좌는 현대 백엔드 및 마이크로서비스 아키텍처에서 자주 혼동되는 **Reverse Proxy**, **Load Balancer**, **API Gateway**의 역할과 차이점을 다룹니다.

* **Reverse Proxy (리버스 프록시)**:
  * 클라이언트와 백엔드 서버 사이에서 중앙 중계자 역할을 수행합니다.
  * 클라이언트는 백엔드 서버의 실제 IP나 내부 구조를 알 수 없으며, 프록시가 제공하는 단일 주소로 요청을 전송합니다.
  * 주요 기능: 보안(내부 IP 은닉), SSL/TLS 터미네이션(암복호화 분리), 캐싱(반복 요청 자체 응답), 헤더 압축 및 보안 필터링.
* **Load Balancer (로드밸런서)**:
  * 단일 서버의 트래픽 과부하를 방지하기 위해 들어오는 요청을 여러 백엔드 서버 인스턴스로 분산(Distribution)합니다.
  * L4(IP/Port 기반) 또는 L7(HTTP URL/Cookie 기반)에서 작동하며, 헬스 체크(Health Check)를 통해 장애가 발생한 서버를 자동으로 트래픽 배정 대상에서 제외합니다.
* **API Gateway (API 게이트웨이)**:
  * 마이크로서비스 아키텍처(MSA) 전단에 위치하는 통합 API 관리 및 라우팅 엔트리 포인트입니다.
  * 단순 트래픽 전송을 넘어 사용자 인증/인가(OAuth, JWT), 처리율 제한(Rate Limiting / Throttling), API 버전 관리, 서비스 디스커버리 연동, 프로토콜 변환(HTTP/REST ↔ gRPC), 로깅 및 모니터링 기능을 제공합니다.

---

### 2) 세 메커니즘 간 비교 정리

| 항목 | Reverse Proxy | Load Balancer | API Gateway |
| :--- | :--- | :--- | :--- |
| **주요 목적** | 서버 보호, SSL 종단, 캐싱 | 트래픽 분산 및 고가용성(HA) 확보 | 마이크로서비스 API 통합 관리 및 라우팅 |
| **주요 계층** | L4 / L7 계층 | L4 / L7 계층 | L7 (응용 계층) 중심 |
| **핵심 기능** | IP 은닉, 캐싱, TLS 복호화 | 라운드로빈, 헬스체크, 트래픽 분배 | 인증/인가, Rate Limiting, 프로토콜 변환, 라우팅 |
| **대표 솔루션** | Nginx, HAProxy | AWS ALB/NLB, HAProxy | Kong, AWS API Gateway, Envoy |

---

### 3) 세부 대본 정리
* **개념적 혼동의 이유**: 세 요소 모두 네트워크 요청을 중간에서 수용하여 백엔드로 전달하는 공통적 특성을 지니고 있어 단일 소프트웨어(예: Nginx, Envoy, HAProxy)가 프록시, 로드밸런서, API 게이트웨이 역할을 동시에 수행할 수 있습니다.
* **포워드 프록시 vs 리버스 프록시**: 포워드 프록시는 클라이언트 편(내부망)에 위치해 클라이언트의 신원을 숨기고, 리버스 프록시는 서버 전단에 위치해 백엔드 서버의 신원과 IP를 보호합니다.
* **마이크로서비스에서의 발전**: 단일 리버스 프록시가 로드밸런싱 기능을 추가하고, 나아가 MSA의 복잡성을 흡수하기 위해 인증, 비즈니스 라우팅, Rate Limiting 기능을 결합하면서 API 게이트웨이 형태로 발전했습니다.


## 32. Reverse Proxy vs Load Balancer vs API Gateway: The Real Difference ?
### 1) 상세 구동 메커니즘
- **Reverse Proxy**: 백엔드 서버 전단에 위치하여 클라이언트의 요청을 받아 실제 서버 대신 응답을 전달하는 대리자 역할을 수행합니다. 서버의 실제 IP 주소를 은닉하여 보안을 강화하며, SSL/TLS 터미네이션, 응답 Caching, Gzip 압축 등을 처리합니다.
- **Load Balancer**: 역방향 프록시의 특화된 형태로, 여러 대의 백엔드 서버 인스턴스로 들어오는 대용량 트래픽을 분산 라우팅(Round Robin, Least Connection, IP Hash 등)하여 시스템의 확장성(Scalability)과 고가용성(Availability)을 보장합니다. L4(IP/TCP 레벨) 및 L7(HTTP URL/헤더 레벨) 계층에서 작동합니다.
- **API Gateway**: 마이크로서비스(MSA) 아키텍처의 통합 진입점(Edge)으로서, Reverse Proxy 및 Load Balancer의 기본 기능을 모두 포함함과 동시에 API 관리 기능(인증/인가, Rate Limiting, 요청/응답 변환, API 버전 관리, 모니터링/로깅)을 통합 제공합니다.

### 2) 세부 기능 비교표
| 구분 | Reverse Proxy | Load Balancer | API Gateway |
| :--- | :--- | :--- | :--- |
| **주 목적** | 서버 보호, Caching, SSL 처리 | 트래픽 분산, 고가용성(HA) 확보 | API 중앙화 관리, 마이크로서비스 관문 |
| **핵심 기능** | IP 은닉, SSL 터미네이션, Caching | Round Robin, Health Check, Failover | Auth, Rate Limiting, Routing, Logging |
| **주요 위치** | 단일/복수 서버 전단 | 서버 클러스터/풀 전단 | MSA 클러스터 최전단 (Edge) |
| **대표 도구** | NGINX, HAProxy, Caddy | AWS ALB/NLB, NGINX, HAProxy | Kong, AWS API Gateway, Apogee |

### 3) 세부 대본 정리
- 클라이언트와 백엔드 서비스 간의 중계 계층을 구성할 때 요구사항에 맞춰 최적의 컴포넌트를 조합해 활용할 수 있습니다.
- 단순 SSL 처리와 정적 Caching이 목적이면 **Reverse Proxy**, 서버 인스턴스 증설에 따른 트래픽 분산이 목적이면 **Load Balancer**, 여러 마이크로서비스 간 인증, 쿼터 제한, 라우팅 통제가 필요하다면 **API Gateway**를 선택합니다.
