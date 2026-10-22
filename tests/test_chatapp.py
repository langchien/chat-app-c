#!/usr/bin/env python3
import socket
import time
import subprocess
import sys
import os

SERVER_PORT = 9988
SERVER_CMD = ["./server", str(SERVER_PORT)]

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

def connect_client():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(("127.0.0.1", SERVER_PORT))
    return s

def send_line(s, line):
    s.sendall((line + "\n").encode('utf-8'))

def recv_line(s, timeout=1.0):
    s.settimeout(timeout)
    data = b""
    while b"\n" not in data:
        try:
            chunk = s.recv(1024)
            if not chunk:
                break
            data += chunk
        except socket.timeout:
            break
    lines = data.decode('utf-8', errors='ignore').splitlines()
    return lines

def run_tests():
    print(">>> Starting Chat Server (Milestone 2 - SQLite3 & Auth)...")
    srv = start_server()

    try:
        # Test 1: Welcome message
        c1 = connect_client()
        lines1 = recv_line(c1)
        print("C1 received on connect:", lines1)
        assert any("Welcome" in l for l in lines1), "Expected welcome message"

        # Test 2.1: Unauthenticated command guard (Task 2.3 / Milestone 2)
        send_line(c1, "MSG alice Hello")
        res_unauth = recv_line(c1)
        print("C1 unauthenticated MSG response:", res_unauth)
        assert any("Please login first" in l for l in res_unauth), "Expected 'Please login first'"

        send_line(c1, "JOIN test_group")
        res_unauth2 = recv_line(c1)
        print("C1 unauthenticated JOIN response:", res_unauth2)
        assert any("Please login first" in l for l in res_unauth2), "Expected 'Please login first'"

        # Test 2.2: Invalid login
        send_line(c1, "LOGIN alice wrongpassword")
        res = recv_line(c1)
        print("C1 invalid login res:", res)
        assert any("ERROR" in l for l in res), "Expected ERROR on wrong pass"

        # Test 3: Successful login with seeded SQLite account
        send_line(c1, "LOGIN alice 123456")
        res = recv_line(c1)
        print("C1 login res:", res)
        assert any("LOGIN_OK" in l for l in res), "Expected LOGIN_OK"

        # Test 3.1: Already logged in on current session
        send_line(c1, "LOGIN bob 123456")
        res_already = recv_line(c1)
        print("C1 re-login on same session:", res_already)
        assert any("Already logged in" in l for l in res_already), "Expected Already logged in"

        # Test 4: Register new user (Task 2.1/2.2: SHA-256 + Salt in SQLite3)
        c2 = connect_client()
        recv_line(c2) # welcome
        send_line(c2, "REGISTER eve password123")
        res = recv_line(c2)
        print("C2 register res:", res)
        assert any("OK" in l for l in res), "Expected OK on register"

        # Duplicate register test
        send_line(c2, "REGISTER eve password123")
        res_dup_reg = recv_line(c2)
        print("C2 duplicate register res:", res_dup_reg)
        assert any("already exists" in l for l in res_dup_reg), "Expected already exists error"

        # Duplicate login test: C2 tries to login as alice
        send_line(c2, "LOGIN alice 123456")
        res = recv_line(c2)
        print("C2 duplicate login res:", res)
        assert any("ERROR" in l and "already logged in" in l for l in res), "Expected duplicate login rejection"

        # Test 5: Presence event when C2 logs in as eve
        send_line(c2, "LOGIN eve password123")
        res_c2 = recv_line(c2)
        print("C2 login res:", res_c2)
        assert any("LOGIN_OK" in l for l in res_c2)

        # C1 should receive EVENT_ONLINE eve
        res_c1 = recv_line(c1)
        print("C1 presence event for eve:", res_c1)
        assert any("EVENT_ONLINE eve" in l for l in res_c1), "C1 should receive EVENT_ONLINE eve"

        # Test 6: 1-to-1 messaging (alice -> eve)
        send_line(c1, "MSG eve Hello Eve!")
        res_c1 = recv_line(c1)
        print("C1 msg ack:", res_c1)
        assert any("OK Sent to eve" in l for l in res_c1)

        res_c2 = recv_line(c2)
        print("C2 received 1-1 msg:", res_c2)
        assert any("FROM alice Hello Eve!" in l for l in res_c2)

        # Test 7: Group join and group chat
        send_line(c1, "JOIN group_test")
        print("C1 join group_test:", recv_line(c1))

        send_line(c2, "JOIN group_test")
        print("C2 join group_test:", recv_line(c2))
        print("C1 saw eve joined group:", recv_line(c1))

        # Send group message from C2
        send_line(c2, "GROUP_MSG group_test Hello everyone in test group!")
        print("C2 group msg ack:", recv_line(c2))

        # C1 receives group message
        res_c1_grp = recv_line(c1)
        print("C1 received group msg:", res_c1_grp)
        assert any("GROUP_FROM group_test eve Hello everyone in test group!" in l for l in res_c1_grp)

        # Test 8: 1-to-1 chat session and disconnect notification
        send_line(c1, "START_CHAT eve")
        print("C1 start chat with eve:", recv_line(c1))
        print("C2 notice start chat:", recv_line(c2))

        # C2 disconnects abruptly
        print("C2 disconnecting...")
        c2.close()

        # C1 should receive NOTICE that eve disconnected AND EVENT_OFFLINE eve
        time.sleep(0.2)
        res_c1_disc = recv_line(c1)
        print("C1 received after C2 disconnect:", res_c1_disc)
        assert any("User 'eve' has disconnected." in l for l in res_c1_disc), "Expected disconnect notice"
        assert any("EVENT_OFFLINE eve" in l for l in res_c1_disc), "Expected offline event"

        # Test 9: Logout test (Task 2.3)
        send_line(c1, "LOGOUT")
        res_logout = recv_line(c1)
        print("C1 logout res:", res_logout)
        assert any("Logged out" in l for l in res_logout), "Expected Logged out"

        # ========================================================
        # Milestone 3 Tests: Realtime Presence, Groups, History, Offline
        # ========================================================
        print("\n>>> Testing Milestone 3 Features (History & Offline sync)...")

        # Test 10: Login alice again
        send_line(c1, "LOGIN alice 123456")
        res_login_c1 = recv_line(c1)
        print("C1 re-login res:", res_login_c1)
        assert any("LOGIN_OK alice" in l for l in res_login_c1)

        # Test 11: 1-1 History command
        send_line(c1, "HISTORY eve 10")
        res_hist = recv_line(c1)
        print("C1 history with eve:", res_hist)
        assert any("HISTORY_START eve" in l for l in res_hist), "Expected HISTORY_START eve"
        assert any("Hello Eve!" in l for l in res_hist), "Expected past 1-1 message in history"
        assert any("HISTORY_END eve" in l for l in res_hist), "Expected HISTORY_END eve"

        # Test 12: Group History command & Group List
        send_line(c1, "GROUPS")
        res_groups = recv_line(c1)
        print("C1 groups list:", res_groups)
        assert any("group_test" in l for l in res_groups), "Expected group_test in GROUPS"

        send_line(c1, "MEMBERS group_test")
        res_members = recv_line(c1)
        print("C1 members list:", res_members)
        assert any("alice" in l for l in res_members), "Expected alice in group members"

        send_line(c1, "HISTORY group_test 10")
        res_grp_hist = recv_line(c1)
        print("C1 group_test history:", res_grp_hist)
        assert any("HISTORY_START group_test" in l for l in res_grp_hist), "Expected HISTORY_START"
        assert any("Hello everyone in test group!" in l for l in res_grp_hist), "Expected group message in history"
        assert any("HISTORY_END group_test" in l for l in res_grp_hist), "Expected HISTORY_END"

        # Test 13: Offline Messaging Queue (Task 3.2, 3.4)
        # Alice sends message to bob (bob is currently offline)
        send_line(c1, "MSG bob Hello Bob, this is an offline message!")
        res_offline_ack = recv_line(c1)
        print("C1 offline msg ack:", res_offline_ack)
        assert any("offline message stored" in l for l in res_offline_ack), "Expected offline storage ack"

        # Bob logs in now (C3)
        c3 = connect_client()
        recv_line(c3) # welcome
        send_line(c3, "LOGIN bob 123456")
        res_bob = recv_line(c3)
        print("C3 (Bob) login response:", res_bob)
        assert any("LOGIN_OK bob" in l for l in res_bob), "Bob should login successfully"
        assert any("OFFLINE_START" in l for l in res_bob), "Bob should receive OFFLINE_START"
        assert any("Hello Bob, this is an offline message!" in l for l in res_bob), "Bob should receive offline message"
        assert any("OFFLINE_END" in l for l in res_bob), "Bob should receive OFFLINE_END"

        # C1 should receive EVENT_ONLINE bob
        res_c1_saw_bob = recv_line(c1)
        print("C1 saw Bob online:", res_c1_saw_bob)
        assert any("EVENT_ONLINE bob" in l for l in res_c1_saw_bob), "C1 should receive EVENT_ONLINE bob"

        # Bob logs out
        send_line(c3, "LOGOUT")
        recv_line(c3)
        c3.close()

        c1.close()
        print("\n>>> ALL TESTS (MILESTONE 1, 2 & 3) PASSED SUCCESSFULLY! <<<")

    finally:
        srv.terminate()
        srv.wait()

if __name__ == '__main__':
    run_tests()

