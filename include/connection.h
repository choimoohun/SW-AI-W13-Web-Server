/*
 * connection.h — 연결 처리 인터페이스 + 연결별 상태. (오늘: 정적 파일까지)
 *
 * [계약] client fd 소유권을 받아 '연결 하나를 블로킹으로' 처리하고 닫는다.
 * 불완전 요청(NEED_MORE)은 더 읽고, Keep-Alive 미지원 단계에서는 Connection: close 후
 * 요청 1건마다 닫는다. 청소는 단일 지점에서 fd·버퍼를 정확히 한 번 해제.
 * 정적 파일 file_fd 도 이 모듈이 닫는다(소유권 계약).
 * 요구사항: FR-02~04, NFR-01/03, FR-06.
 */
#ifndef CONNECTION_H
#define CONNECTION_H

/* client_fd 를 받아 요청 읽기→파싱→정적 파일 응답→닫기 까지 수행.
 * doc_root: 정적 파일 루트. 호출 후 client_fd 는 이 함수가 닫는다. */
void connection_handle(int client_fd, const char *doc_root);

#endif /* CONNECTION_H */
