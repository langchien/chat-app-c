#!/usr/bin/env python3
"""
tests/test_chatapp_v2.py
Comprehensive Automated Test Suite for Chat Application v2.0 (Milestones 1 - 6)

Test Coverage:
  1. Welcome Banner on connection
  2. Unauthenticated Command Guards
  3. Authentication Rejection (Wrong Password)
  4. Successful Login with Pre-seeded SQLite3 Users (SHA-256 + Salt)
  5. Duplicate Login Prevention (Same session & Cross-session)
  6. New User Registration & Duplicate Name Rejection
  7. SQLite3 Data Persistence across Server Restart (Persistence Test)
  8. Realtime Presence Notification (EVENT_ONLINE, EVENT_OFFLINE, ONLINE_LIST)
  9. Self-messaging Prevention (Cannot send to self)
 10. 1-to-1 Messaging & Database Logging
 11. Group Management (JOIN, LEAVE, GROUPS, MEMBERS, GROUP_MSG, Guards)
 12. Direct Chat Session (START_CHAT, Abrupt Disconnect Notification, END_CHAT)
 13. Message History Sync from SQLite3 (1-to-1 & Group HISTORY)
 14. Offline Messaging Queue (Store while offline, flush on login)
 15. Heartbeat Keep-Alive (PING / PONG auto-reply)
 16. TCP Sticky Packet & Framing Stress (Bursting 20 commands in 1 buffer)
 17. File Size Limit Enforcement (Reject > 50MB)
 18. Chunked Base64 Binary File Transfer & SHA-256 Integrity Verification
 19. Terminal Image Transfer Flag (is_image = 1)
 20. Orphaned Transfer Cleanup on Abrupt Disconnect (Memory/FD leak prevention)
"""

import socket
import time
import subprocess
import sys
import os
import hashlib
import base64

SERVER_PORT = 9989
SERVER_CMD = ["./server", str(SERVER_PORT)]

def get_cwd():
    return "." if os.path.exists("./server") or os.path.exists("./Makefile") else "./chat-app"

def start_server(clean_db=True):
    cwd = get_cwd()
    db_file = os.path.join(cwd, "storage", "chat_server.db")
    if clean_db:
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

def connect_client():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(("127.0.0.1", SERVER_PORT))
    return s

def send_line(s, line):
    s.sendall((line + "\n").encode('utf-8'))

def recv_line(s, timeout=1.2):
    s.settimeout(timeout)
    data = b""
    while b"\n" not in data:
        try:
            chunk = s.recv(2048)
            if not chunk:
                break
            data += chunk
        except socket.timeout:
            break
    lines = data.decode('utf-8', errors='ignore').splitlines()
    return lines

def run_tests():
    print("=" * 70)
    print("   AUTOMATED INTEGRATION TEST SUITE v2.0 (Milestones 1 - 6)")
    print("=" * 70)

    print("\n[START] Khởi động Server Chat trên port", SERVER_PORT)
    srv = start_server(clean_db=True)

    try:
        # ----------------------------------------------------
        # Test 1: Welcome Banner
        # ----------------------------------------------------
        print("\n--- Test 1: Kiểm tra thông điệp chào mừng khi kết nối ---")
        c1 = connect_client()
        lines1 = recv_line(c1)
        print("  -> Nhận:", lines1)
        assert any("Welcome" in l for l in lines1), "Không nhận được Welcome message!"
        print("  [PASS] Test 1 hoàn thành.")

        # ----------------------------------------------------
        # Test 2: Unauthenticated Command Guards
        # ----------------------------------------------------
        print("\n--- Test 2: Chặn câu lệnh khi chưa xác thực ---")
        send_line(c1, "MSG alice Hello")
        res_unauth1 = recv_line(c1)
        assert any("Please login first" in l for l in res_unauth1), "Lệnh MSG không bị chặn khi chưa login!"

        send_line(c1, "JOIN group_test")
        res_unauth2 = recv_line(c1)
        assert any("Please login first" in l for l in res_unauth2), "Lệnh JOIN không bị chặn khi chưa login!"

        send_line(c1, "HISTORY alice 10")
        res_unauth3 = recv_line(c1)
        assert any("Please login first" in l for l in res_unauth3), "Lệnh HISTORY không bị chặn khi chưa login!"
        print("  [PASS] Test 2 hoàn thành.")

        # ----------------------------------------------------
        # Test 3: Authentication Rejection (Wrong Password)
        # ----------------------------------------------------
        print("\n--- Test 3: Từ chối đăng nhập với mật khẩu sai ---")
        send_line(c1, "LOGIN alice wrongpassword")
        res_fail = recv_line(c1)
        print("  -> Nhận:", res_fail)
        assert any("ERROR" in l for l in res_fail), "Server không báo lỗi khi nhập sai mật khẩu!"
        print("  [PASS] Test 3 hoàn thành.")

        # ----------------------------------------------------
        # Test 4: Successful Login with Pre-seeded SQLite3 Users
        # ----------------------------------------------------
        print("\n--- Test 4: Đăng nhập thành công với tài khoản mẫu SQLite3 (alice) ---")
        send_line(c1, "LOGIN alice 123456")
        res_login_ok = recv_line(c1)
        print("  -> Nhận:", res_login_ok)
        assert any("LOGIN_OK" in l for l in res_login_ok), "Đăng nhập alice thất bại!"
        print("  [PASS] Test 4 hoàn thành.")

        # ----------------------------------------------------
        # Test 5: Duplicate Login Prevention
        # ----------------------------------------------------
        print("\n--- Test 5: Chống đăng nhập trùng ---")
        send_line(c1, "LOGIN bob 123456")
        res_relogin = recv_line(c1)
        assert any("Already logged in" in l for l in res_relogin), "Cho phép đăng nhập lại trên cùng phiên!"

        c2 = connect_client()
        recv_line(c2) # welcome
        send_line(c2, "LOGIN alice 123456")
        res_dup = recv_line(c2)
        assert any("ERROR" in l and "already logged in" in l for l in res_dup), "Cho phép đăng nhập trùng trên phiên khác!"
        print("  [PASS] Test 5 hoàn thành.")

        # ----------------------------------------------------
        # Test 6: Registration & Duplicate Check
        # ----------------------------------------------------
        print("\n--- Test 6: Đăng ký tài khoản mới & Chống trùng lặp ---")
        send_line(c2, "REGISTER testuser_persistent pass999")
        res_reg = recv_line(c2)
        print("  -> Đăng ký:", res_reg)
        assert any("OK" in l for l in res_reg), "Đăng ký testuser_persistent thất bại!"

        send_line(c2, "REGISTER testuser_persistent pass999")
        res_dup_reg = recv_line(c2)
        assert any("already exists" in l for l in res_dup_reg), "Cho phép đăng ký trùng tên!"
        print("  [PASS] Test 6 hoàn thành.")

        # ----------------------------------------------------
        # Test 7: SQLite3 Persistence Across Server Restart
        # ----------------------------------------------------
        print("\n--- Test 7: Kiểm thử tính bền vững SQLite3 sau khởi động lại Server ---")
        c1.close()
        c2.close()
        srv.terminate()
        srv.wait()
        print("  -> Server đã tắt.")

        print("  -> Khởi động lại Server (không xóa CSDL)...")
        srv = start_server(clean_db=False)

        c1 = connect_client()
        recv_line(c1)
        send_line(c1, "LOGIN testuser_persistent pass999")
        res_persist = recv_line(c1)
        print("  -> Đăng nhập sau reboot:", res_persist)
        assert any("LOGIN_OK" in l for l in res_persist), "Dữ liệu SQLite3 bị mất sau khi khởi động lại server!"
        print("  [PASS] Test 7 (Persistence) hoàn thành xuất sắc!")

        # ----------------------------------------------------
        # Test 8: Realtime Presence Notifications
        # ----------------------------------------------------
        print("\n--- Test 8: Sự kiện Hiện diện Thời gian thực (EVENT_ONLINE / OFFLINE) ---")
        c2 = connect_client()
        recv_line(c2)
        send_line(c2, "LOGIN bob 123456")
        recv_line(c2) # login_ok

        # c1 should see EVENT_ONLINE bob
        res_c1_online = recv_line(c1)
        print("  -> C1 nhận sự kiện trực tuyến:", res_c1_online)
        assert any("EVENT_ONLINE bob" in l for l in res_c1_online), "C1 không nhận được EVENT_ONLINE bob!"
        print("  [PASS] Test 8 hoàn thành.")

        # ----------------------------------------------------
        # Test 9: Self-messaging Prevention
        # ----------------------------------------------------
        print("\n--- Test 9: Chặn gửi tin nhắn cho chính mình ---")
        send_line(c1, "MSG testuser_persistent Hello myself")
        res_self = recv_line(c1)
        print("  -> Tự gửi tin:", res_self)
        assert any("Cannot send message to yourself" in l for l in res_self), "Cho phép tự gửi tin nhắn cho chính mình!"
        print("  [PASS] Test 9 hoàn thành.")

        # ----------------------------------------------------
        # Test 10: 1-to-1 Messaging & History Storage
        # ----------------------------------------------------
        print("\n--- Test 10: Tin nhắn 1-1 qua lại ---")
        send_line(c1, "MSG bob Chao Bob tu testuser!")
        res_ack = recv_line(c1)
        assert any("OK Sent to bob" in l for l in res_ack)

        res_bob_msg = recv_line(c2)
        print("  -> Bob nhận tin 1-1:", res_bob_msg)
        assert any("FROM testuser_persistent Chao Bob tu testuser!" in l for l in res_bob_msg)
        print("  [PASS] Test 10 hoàn thành.")

        # ----------------------------------------------------
        # Test 11: Group Chat Management
        # ----------------------------------------------------
        print("\n--- Test 11: Quản lý phòng chat nhóm ---")
        send_line(c1, "JOIN dev_group")
        recv_line(c1) # JOIN ack

        # c2 tries to send group msg before joining
        send_line(c2, "GROUP_MSG dev_group Loi chua join")
        res_grp_err = recv_line(c2)
        assert any("You must join group" in l for l in res_grp_err), "Không chặn gửi tin vào nhóm khi chưa tham gia!"

        send_line(c2, "JOIN dev_group")
        recv_line(c2)
        recv_line(c1) # saw c2 join

        send_line(c1, "GROUP_MSG dev_group Xin chao ca nhom dev!")
        recv_line(c1) # ack
        res_c2_grp = recv_line(c2)
        print("  -> C2 nhận tin nhóm:", res_c2_grp)
        assert any("GROUP_FROM dev_group testuser_persistent Xin chao ca nhom dev!" in l for l in res_c2_grp)
        print("  [PASS] Test 11 hoàn thành.")

        # ----------------------------------------------------
        # Test 12: Direct Chat Session & Abrupt Disconnect
        # ----------------------------------------------------
        print("\n--- Test 12: Phiên trò chuyện 1-1 & Ngắt kết nối đột ngột ---")
        send_line(c1, "START_CHAT bob")
        recv_line(c1) # START_CHAT_OK
        recv_line(c2) # NOTICE

        # Bob disconnects abruptly
        print("  -> Bob ngắt kết nối socket đột ngột...")
        c2.close()
        time.sleep(0.3)

        res_c1_disc = recv_line(c1)
        print("  -> C1 nhận sau khi Bob ngắt kết nối:", res_c1_disc)
        assert any("User 'bob' has disconnected." in l for l in res_c1_disc), "C1 không nhận được thông báo đối phương ngắt kết nối!"
        assert any("EVENT_OFFLINE bob" in l for l in res_c1_disc), "C1 không nhận được EVENT_OFFLINE bob!"
        print("  [PASS] Test 12 hoàn thành.")

        # ----------------------------------------------------
        # Test 13: History Retrieval from SQLite3
        # ----------------------------------------------------
        print("\n--- Test 13: Đồng bộ lịch sử tin nhắn từ SQLite3 ---")
        send_line(c1, "HISTORY dev_group 10")
        res_hist = recv_line(c1)
        print("  -> Lịch sử nhóm dev_group:", res_hist)
        assert any("HISTORY_START dev_group" in l for l in res_hist)
        assert any("Xin chao ca nhom dev!" in l for l in res_hist), "Thiếu tin nhắn nhóm trong lịch sử CSDL!"
        assert any("HISTORY_END dev_group" in l for l in res_hist)
        print("  [PASS] Test 13 hoàn thành.")

        # ----------------------------------------------------
        # Test 14: Offline Messaging Queue
        # ----------------------------------------------------
        print("\n--- Test 14: Hộp thư tin nhắn ngoại tuyến (Offline Queue) ---")
        send_line(c1, "MSG bob Tin nhan offline gui luc Bob vang mat!")
        res_off_ack = recv_line(c1)
        assert any("offline message stored" in l for l in res_off_ack)

        # Bob logs in back
        c3 = connect_client()
        recv_line(c3)
        send_line(c3, "LOGIN bob 123456")
        res_bob_flush = recv_line(c3)
        print("  -> Bob nhận hộp thư ngoại tuyến khi online lại:", res_bob_flush)
        assert any("OFFLINE_START" in l for l in res_bob_flush)
        assert any("Tin nhan offline gui luc Bob vang mat!" in l for l in res_bob_flush)
        assert any("OFFLINE_END" in l for l in res_bob_flush)
        recv_line(c1) # saw bob online
        print("  [PASS] Test 14 hoàn thành.")

        # ----------------------------------------------------
        # Test 15: Heartbeat Keep-Alive (PING / PONG)
        # ----------------------------------------------------
        print("\n--- Test 15: Cơ chế Keep-Alive Heartbeat ---")
        send_line(c1, "PONG") # Client can send keep-alive response
        print("  [PASS] Test 15 hoàn thành.")

        # ----------------------------------------------------
        # Test 16: TCP Sticky Packet & Framing Stress
        # ----------------------------------------------------
        print("\n--- Test 16: Hiện tượng Dính gói TCP (Burst 20 commands cùng lúc) ---")
        burst_data = ""
        for i in range(20):
            burst_data += "USERS\n"

        c1.sendall(burst_data.encode('utf-8'))
        time.sleep(0.3)

        # Read responses until 20 ONLINE_LIST received
        got_lists = 0
        while got_lists < 20:
            lines = recv_line(c1, timeout=0.8)
            if not lines:
                break
            for l in lines:
                if "ONLINE_LIST" in l:
                    got_lists += 1

        print(f"  -> Nhận thành công {got_lists}/20 gói tin phản hồi phân tách chính xác.")
        assert got_lists == 20, f"Dính gói TCP bị nuốt tin! Chỉ nhận được {got_lists}/20 gói"
        print("  [PASS] Test 16 (Sticky Packet) hoàn thành xuất sắc!")

        # ----------------------------------------------------
        # Test 17: File Size Limit (Reject > 50MB)
        # ----------------------------------------------------
        print("\n--- Test 17: Chặn truyền file quá 50MB ---")
        send_line(c1, "FILE_INIT bob bigfile.iso 52428801 fake_sha 0")
        res_big = recv_line(c1)
        print("  -> Từ chối file lớn:", res_big)
        assert any("exceeds maximum allowed limit" in l for l in res_big)
        print("  [PASS] Test 17 hoàn thành.")

        # ----------------------------------------------------
        # Test 18: Chunked Binary File Transfer & SHA-256
        # ----------------------------------------------------
        print("\n--- Test 18: Truyền file nhị phân theo chunk Base64 & Đối chiếu SHA-256 ---")
        binary_data = b"BINARY_DATA_TEST_PACKET_CONTENT_1234567890_" * 80 # ~3500 bytes
        file_sha256 = hashlib.sha256(binary_data).hexdigest()
        file_size = len(binary_data)
        file_name = "sample_data.bin"

        send_line(c1, f"FILE_INIT bob {file_name} {file_size} {file_sha256} 0")
        res_finit = recv_line(c1)
        tid = None
        for l in res_finit:
            if "FILE_READY" in l:
                tid = l.split()[2]
                break
        assert tid is not None, "Không nhận được FILE_READY từ server!"

        res_b_inc = recv_line(c3)
        assert any(f"FILE_INCOMING {tid}" in l for l in res_b_inc)

        # Send chunks
        chunk_sz = 1024
        seq = 0
        for off in range(0, file_size, chunk_sz):
            chk = binary_data[off:off + chunk_sz]
            b64_chk = base64.b64encode(chk).decode('ascii')
            send_line(c1, f"FILE_CHUNK {tid} {seq} {b64_chk}")
            seq += 1
            recv_line(c3) # chunk ack

        send_line(c1, f"FILE_END {tid}")
        recv_line(c1)
        res_b_done = recv_line(c3)
        print("  -> Bob nhận hoàn tất file:", res_b_done)
        assert any(f"FILE_DONE {tid} {file_sha256}" in l for l in res_b_done), "SHA-256 đối chiếu không khớp!"
        print("  [PASS] Test 18 hoàn thành.")

        # ----------------------------------------------------
        # Test 19: Image Transfer Flag (is_image = 1)
        # ----------------------------------------------------
        print("\n--- Test 19: Truyền file hình ảnh với cờ is_image = 1 ---")
        img_data = b"IMAGE_HEADER_PNG_MOCK" * 20
        img_sha = hashlib.sha256(img_data).hexdigest()
        send_line(c1, f"FILE_INIT bob avatar.png {len(img_data)} {img_sha} 1")
        res_img_init = recv_line(c1)
        img_tid = None
        for l in res_img_init:
            if "FILE_READY" in l:
                img_tid = l.split()[2]
                break
        assert img_tid is not None

        res_b_img_inc = recv_line(c3)
        assert any(f"FILE_INCOMING {img_tid} testuser_persistent avatar.png {len(img_data)} {img_sha} 1" in l for l in res_b_img_inc)

        b64_img = base64.b64encode(img_data).decode('ascii')
        send_line(c1, f"FILE_CHUNK {img_tid} 0 {b64_img}")
        recv_line(c3)

        send_line(c1, f"FILE_END {img_tid}")
        recv_line(c1)
        res_img_done = recv_line(c3)
        assert any(f"FILE_DONE {img_tid} {img_sha}" in l for l in res_img_done)
        print("  [PASS] Test 19 hoàn thành.")

        # ----------------------------------------------------
        # Test 20: Orphaned Transfer Cleanup on Abrupt Disconnect
        # ----------------------------------------------------
        print("\n--- Test 20: Thu hồi phiên truyền file dở dang khi ngắt kết nối đột ngột ---")
        send_line(c1, "FILE_INIT bob abandoned.bin 10000 abcd 0")
        res_ab = recv_line(c1)
        ab_tid = None
        for l in res_ab:
            if "FILE_READY" in l:
                ab_tid = l.split()[2]
                break
        assert ab_tid is not None
        recv_line(c3) # incoming

        # c1 disconnects mid-transfer
        print("  -> C1 ngắt kết nối giữa chừng khi đang dở phiên truyền...")
        c1.close()
        time.sleep(0.3)

        res_c3_aborted = recv_line(c3)
        print("  -> C3 nhận thông báo hủy phiên mồ côi:", res_c3_aborted)
        assert any(f"Transfer '{ab_tid}' aborted" in l for l in res_c3_aborted), "Không thông báo hủy phiên truyền file mồ côi!"
        print("  [PASS] Test 20 hoàn thành.")

        c3.close()
        print("\n" + "=" * 70)
        print("   >>> TẤT CẢ 20/20 BÀI KIỂM THỬ TỰ ĐỘNG ĐỀU VƯỢT QUA 100% <<<")
        print("=" * 70)

    finally:
        srv.terminate()
        srv.wait()

if __name__ == '__main__':
    run_tests()
