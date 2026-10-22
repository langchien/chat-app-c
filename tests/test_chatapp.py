#!/usr/bin/env python3
import socket
import time
import subprocess
import sys
import os

SERVER_PORT = 9988
SERVER_CMD = ["./server", str(SERVER_PORT)]

def start_server():
    p = subprocess.Popen(SERVER_CMD, cwd="./chat-app", stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(0.5)
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
    print(">>> Starting Chat Server...")
    srv = start_server()

    try:
        # Test 1: Welcome message
        c1 = connect_client()
        lines1 = recv_line(c1)
        print("C1 received on connect:", lines1)
        assert any("Welcome" in l for l in lines1), "Expected welcome message"

        # Test 2: Invalid login
        send_line(c1, "LOGIN alice wrongpassword")
        res = recv_line(c1)
        print("C1 invalid login res:", res)
        assert any("ERROR" in l for l in res), "Expected ERROR on wrong pass"

        # Test 3: Successful login
        send_line(c1, "LOGIN alice 123456")
        res = recv_line(c1)
        print("C1 login res:", res)
        assert any("LOGIN_OK" in l for l in res), "Expected LOGIN_OK"

        # Test 4: Register new user and duplicate login test
        c2 = connect_client()
        recv_line(c2) # welcome
        send_line(c2, "REGISTER eve password123")
        res = recv_line(c2)
        print("C2 register res:", res)
        assert any("OK" in l for l in res), "Expected OK on register"

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
        # C1 gets notification that eve joined group
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

        c1.close()
        print("\n>>> ALL TESTS PASSED SUCCESSFULLY! <<<")

    finally:
        srv.terminate()
        srv.wait()

if __name__ == '__main__':
    run_tests()
