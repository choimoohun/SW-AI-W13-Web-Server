# STEP10 — 결제 서비스 연결

예약과 결제를 별도 프로세스/별도 DB로 분리한 단계입니다.
결제는 승인·환불·재시도 정책이 예약과 다르고, 독립 배포와 재시작이 필요합니다.

## 구성

```text
/api/payments/*  →  게이트웨이(rewrite /)  →  결제 프로세스(8500)
                                              →  자기 파일 DB
```

- `tests/backends/serve_payments.py`: 표준 라이브러리 `http.server` 기반
- `PAYMENT_DATA_FILE`: 테스트마다 다른 DB 파일 사용
- `conf/backends.conf`에 라우트 추가

## 중복 방지 키

같은 결제 요청이 여러 번 와도 한 번만 청구해야 합니다.
요청 본문의 `idempotency_key`가 그 역할을 합니다.

프록시가 `Idempotency-Key` 같은 임의 헤더를 전달하지 않기 때문에, 키는
헤더가 아니라 본문에도 둡니다. 같은 키가 이미 저장돼 있으면 새 결제를 만들지
않고 기존 결과를 반환합니다.

## 화면 흐름

```text
항공편 검색 → 상세에서 예약하기
  → 오른쪽 패널에 결제 정보 표시
  → "결제하기" 클릭 시 /pay.html?rid=... 로 이동
  → 결제 완료 후 "예약 화면으로 돌아가기"
  → 결제된 예약만 "내 예약"에 표시
```

`www/flights.js`의 `renderPaymentPanel()`이 오른쪽 패널을 만들고,
`www/pay.js`가 결제 페이지를 처리합니다.

## 환불 기간

`flights.js`의 `refundPolicy()`가 `/api/ops/clock`의 UTC epoch을 기준으로
남은 시간을 계산합니다. 시뮬레이션 시계 모드에서도 어긋나지 않습니다.

```text
출발 7일보다 많이 남음 ~ 5일 전 : 100% 환불
출발 5~2일 전                  : 80% 환불
출발 2~1일 전                  : 50% 환불
출발 1일 이내                  : 환불 신청 마감
```

신청 가능 구간에서만 `환불 요청` 버튼이 보이고, 결제 서비스의
`POST /api/payments/{id}/refund`를 `{}` 본문과 함께 호출합니다.

## 검증

```sh
make integration-backends   # 결제 프로세스까지 함께 기동
make integration-payments   # 충전·중복 방지·조회·환불·독립 재시작
```

실제 결과: 201/CAPTURED, 같은 키로 재요청 시 같은 ID, 조회 성공, 환불 성공,
잘못된 금액 400, 프로세스 종료 시 502, 재시작 후 상태 복원까지 전부 PASS.
