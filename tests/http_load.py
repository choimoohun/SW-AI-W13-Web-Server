"""Measure local HTTP traffic and booking edge cases using a built web_server."""

import argparse
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager
from http.client import HTTPConnection, HTTPException
import json
import math
from pathlib import Path
import socket
import subprocess
from threading import Barrier
from time import perf_counter, sleep


HOST = "127.0.0.1"
RESERVATION = b'{"trainId":"KTX101","seatId":"1A"}'
HOME = ("GET", "/", None, 200)
SEATS = ("GET", "/api/seats", None, 200)
BOOK = ("POST", "/api/reservations", RESERVATION, 201)
MIX = {
    "static": HOME,
    "seats": SEATS,
    "missing": ("GET", "/api/missing", None, 404),
    "invalid_booking": ("POST", "/api/reservations", b"{}", 400),
    "duplicate": (*BOOK[:3], 409),
}


def free_port():
    with socket.socket() as listener:
        listener.bind((HOST, 0))
        return listener.getsockname()[1]


@contextmanager
def server(binary, root, port, *options):
    with subprocess.Popen(
        [binary, str(port), root, HOST, *options], stderr=subprocess.PIPE, text=True
    ) as process:
        try:
            startup = process.stderr.readline()
            if not startup.startswith(f"Listening on {HOST}:{port} "):
                raise RuntimeError(startup.strip() or "Server exited before listening")
            yield process.pid
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    raise


def request(port, case):
    method, path, body, _ = case
    headers = {"Content-Type": "application/json"} if body is not None else {}
    start = perf_counter()
    conn = HTTPConnection(HOST, port, timeout=8)
    try:
        conn.request(method, path, body, headers)
        response = conn.getresponse()
        payload = response.read()
        status = response.status
    except (OSError, HTTPException) as error:
        status, payload = type(error).__name__, b""
    finally:
        conn.close()
    return status, (perf_counter() - start) * 1000, payload


def summarize(samples):
    status_counts = Counter(status for status, _ in samples)
    by_status = defaultdict(list)
    for status, ms in samples:
        by_status[status].append(ms)
    return {
        "count": len(samples),
        "statuses": status_counts,
        "p95_ms": {
            status: round(sorted(times)[math.ceil(.95 * len(times)) - 1], 3)
            for status, times in by_status.items()
        },
    }


def measure(port, cases, concurrency):
    labels = tuple(cases)
    gate = Barrier(concurrency + 1)

    def client(index):
        samples = defaultdict(list)
        gate.wait()
        while perf_counter() < deadline:
            label = labels[index % len(labels)]
            status, ms, _ = request(port, cases[label])
            samples[label].append((status, ms))
            index += 1
        return samples

    merged = {label: [] for label in labels}
    with ThreadPoolExecutor(max_workers=concurrency) as pool:
        clients = [pool.submit(client, index) for index in range(concurrency)]
        start = perf_counter()
        deadline = start + 4
        gate.wait()
        for item in clients:
            for label, samples in item.result().items():
                merged[label].extend(samples)
    elapsed = perf_counter() - start
    results = {}
    for label, samples in merged.items():
        expected = cases[label][3]
        results[label] = entry = summarize(samples)
        entry["expected"] = expected
        entry["expected_per_sec"] = round(entry["statuses"][expected] / elapsed, 1)
        entry["unexpected"] = sum(
            count for status, count in entry["statuses"].items()
            if status not in (expected, 503)
        )
    return {"concurrency": concurrency, "seconds": round(elapsed, 3), "cases": results}


def contention(front, count):
    gate = Barrier(count)

    def attempt():
        gate.wait()
        return request(front, BOOK)[:2]

    with ThreadPoolExecutor(max_workers=count) as pool:
        attempts = [pool.submit(attempt) for _ in range(count)]
        return [attempt.result() for attempt in attempts]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    root = Path(__file__).resolve().parents[1] / "public"
    front, backend = free_port(), free_port()
    while front == backend:
        backend = free_port()
    load = []
    races = []
    events = defaultdict(list)
    checks = Counter()
    fd_counts = {}

    def sample(label, case, expected):
        status, ms, payload = request(front, case)
        events[label].append((status, ms))
        checks[label] += status == expected
        return payload if status == expected else None

    with server(binary, root, front, "--proxy", HOST, str(backend)) as front_pid:
        with server(binary, root, backend, "--booking") as booking_pid:
            if request(front, SEATS)[0] != 200:
                raise RuntimeError("Booking API did not respond to the warm-up request")
            sleep(.25)
            pids = {"proxy": front_pid, "booking": booking_pid}
            fd_counts["before"] = {
                name: len(list(Path(f"/proc/{pid}/fd").iterdir())) for name, pid in pids.items()
            }
            for name, case in (("static", HOME), ("seats", SEATS)):
                for concurrency in (4, 16):
                    load.append(measure(front, {name: case}, concurrency))
            sample("seed_reservation", BOOK, 201)
            for concurrency in (4, 16):
                load.append(measure(front, MIX, concurrency))
            sleep(.25)
            fd_counts["after"] = {
                name: len(list(Path(f"/proc/{pid}/fd").iterdir())) for name, pid in pids.items()
            }

        for _ in range(12):
            sample("api_while_backend_down", SEATS, 502)
            sample("static_while_backend_down", HOME, 200)
            with server(binary, root, backend, "--booking"):
                sample("api_after_restart", SEATS, 200)
                batch = contention(front, 8)
                races.extend(batch)
                checks["contention_batches"] += Counter(status for status, _ in batch) == {201: 1, 409: 7}
                payload = sample("read_after_contention", SEATS, 200)
                if payload:
                    seats = json.loads(payload)["seats"]
                    checks["reserved_after_contention"] += any(
                        seat["id"] == "1A" and not seat["available"] for seat in seats
                    )

    print(json.dumps({
        "load": load, "contention": summarize(races),
        "functional": {label: summarize(samples) for label, samples in events.items()},
        "checks": checks, "file_descriptors": fd_counts,
    }, indent=2))
    return int(
        checks["seed_reservation"] != 1
        or any(checks[label] != 12 for label in (
            "contention_batches", "reserved_after_contention", "api_while_backend_down",
            "static_while_backend_down", "api_after_restart", "read_after_contention",
        ))
        or fd_counts["before"] != fd_counts["after"]
        or any(
            entry["unexpected"] or not entry["statuses"].get(entry["expected"])
            for row in load for entry in row["cases"].values()
        )
    )


if __name__ == "__main__":
    raise SystemExit(main())
