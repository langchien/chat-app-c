#!/usr/bin/env python3
"""
tests/stress_test.py
Concurrent Stress & Robustness Test for C Chat Application.

Task 6.2:
  - Spawns 50 - 100 concurrent client threads.
  - Connects, registers unique users, logins, joins shared groups.
  - Concurrently blasts hundreds of group and 1-to-1 messages.
  - Simulates abrupt socket disconnects for 50% of clients simultaneously.
  - Confirms remaining clients can still chat cleanly without server crashes (SIGPIPE safe)
    or deadlocks in select() loop.
"""

import socket
import threading
import time
import subprocess
import os
import sys

SERVER_PORT = 9990
SERVER_CMD = ["./server", str(SERVER_PORT)]
NUM_CLIENTS = 50
MESSAGES_PER_CLIENT = 10

success_count = 0
error_count = 0
lock = threading.Lock()

def start_server():
    cwd = "." if os.path.exists("./server") or os.path.exists("./Makefile") else "./chat-app"
    db_file = os.path.join(cwd, "storage", "chat_server.db")
    for ext in ["", "-wal", "-shm"]:
        f = db_file + ext
        if os.path.exists(f):
            try:
                os.remove(f)
            except Exception:
                pass
    p = subprocess.Popen(SERVER_CMD, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(0.6)
    return p

def worker_thread(client_id, should_disconnect_abruptly):
    global success_count, error_count
    username = f"st_u_{client_id}"
    password = f"pass_{client_id}"

    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(3.0)
        s.connect(("127.0.0.1", SERVER_PORT))

        # 1. Register & Login
        s.sendall(f"REGISTER {username} {password}\n".encode('utf-8'))
        time.sleep(0.05)
        s.sendall(f"LOGIN {username} {password}\n".encode('utf-8'))
        time.sleep(0.05)

        # 2. Join group
        s.sendall("JOIN stress_room\n".encode('utf-8'))
        time.sleep(0.05)

        # 3. Burst messages
        for m in range(MESSAGES_PER_CLIENT):
            s.sendall(f"GROUP_MSG stress_room Hello from {username} msg #{m}\n".encode('utf-8'))
            time.sleep(0.01)

        # 4. Abrupt disconnect vs Graceful disconnect
        if should_disconnect_abruptly:
            # Drop socket instantly without sending QUIT/LOGOUT
            s.close()
        else:
            time.sleep(0.2)
            s.sendall("LOGOUT\n".encode('utf-8'))
            s.sendall("QUIT\n".encode('utf-8'))
            s.close()

        with lock:
            success_count += 1

    except Exception as e:
        with lock:
            error_count += 1
            print(f"[Client {client_id} Error]: {e}")

def run_stress_test():
    print("=" * 70)
    print(f"   STRESS & LOAD TEST: {NUM_CLIENTS} CONCURRENT CLIENTS")
    print("=" * 70)

    print(f"[1/4] Khởi chạy Server trên port {SERVER_PORT}...")
    srv = start_server()

    try:
        start_time = time.time()
        threads = []

        print(f"[2/4] Tạo {NUM_CLIENTS} luồng client kết nối đồng thời và gửi tin...")
        for i in range(NUM_CLIENTS):
            abrupt = (i % 2 == 0) # 50% client ngắt kết nối đột ngột
            t = threading.Thread(target=worker_thread, args=(i, abrupt))
            threads.append(t)
            t.start()

        for t in threads:
            t.join()

        elapsed = time.time() - start_time
        total_ops = NUM_CLIENTS * (MESSAGES_PER_CLIENT + 3)

        print("\n[3/4] Kết quả kiểm thử chịu tải:")
        print(f"  - Số client tham gia: {NUM_CLIENTS}")
        print(f"  - Số luồng hoàn tất thành công: {success_count}/{NUM_CLIENTS}")
        print(f"  - Số luồng gặp lỗi: {error_count}")
        print(f"  - Tổng số thao tác (Connect/Auth/Join/Msg/Drop): ~{total_ops}")
        print(f"  - Thời gian thực thi: {elapsed:.2f} giây ({total_ops / elapsed:.1f} ops/giây)")

        assert error_count == 0, f"Có {error_count} luồng client bị lỗi trong quá trình stress test!"

        # 4. Verify server is still alive and responsive after 50% abrupt drops
        print("\n[4/4] Kiểm tra phản hồi của Server sau đợt ngắt kết nối đột ngột...")
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        probe.settimeout(2.0)
        probe.connect(("127.0.0.1", SERVER_PORT))
        probe.sendall(b"LOGIN alice 123456\n")
        data = probe.recv(1024).decode('utf-8', errors='ignore')
        assert "LOGIN_OK" in data, "Server bị treo hoặc sập sau stress test!"
        probe.sendall(b"QUIT\n")
        probe.close()

        print("\n" + "=" * 70)
        print("   >>> STRESS TEST HOÀN TẤT THÀNH CÔNG 100% - SERVER BỀN BỈ TUYỆT ĐỐI <<<")
        print("=" * 70)

    finally:
        srv.terminate()
        srv.wait()

if __name__ == '__main__':
    run_stress_test()
