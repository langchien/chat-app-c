# Ứng dụng Chat (v2.0 – Modular Architecture)

Mô tả: Nhắn tin một-một và theo nhóm, có hiện diện người dùng.
Yêu cầu chức năng mạng:
- Quản lý nhiều phiên người dùng đồng thời trên một server.
- Đẩy danh sách người đang online, cập nhật khi có người vào hoặc rời.
- Chuyển tiếp tin nhắn một-một và phát tin tới mọi thành viên của nhóm.
- Thông báo cho bên còn lại khi một phía ngắt kết nối hoặc dừng cuộc trò chuyện.
- Xác thực người dùng: Yêu cầu tên đăng nhập và mật khẩu.
- **[Mới - M1]** Cơ chế Heartbeat (PING/PONG) phát hiện Half-Open Connection.
- **[Mới - M1]** Graceful Shutdown – Bắt SIGINT để đóng tất cả kết nối an toàn.
- **[Mới - M2]** Lưu trữ cơ sở dữ liệu nhúng SQLite3 (`storage/chat_server.db`) với chế độ WAL.
- **[Mới - M2]** Bảo mật mật khẩu: Băm SHA-256 kết hợp Salt ngẫu nhiên 16 bytes.
- **[Mới - M2]** 100% Prepared Statements chống tấn công SQL Injection.

---

## 1. Cấu trúc thư mục (Modular)

```
chat-app/
├── Makefile                    # Biên dịch đa module (-Wall -Wextra -Werror -g -O2 -lsqlite3 -lpthread)
├── include/
│   ├── protocol.h              # Định nghĩa lệnh, mã lỗi, hằng số giao thức
│   ├── server.h                # Cấu trúc ClientSession, Group, hàm API server
│   ├── database.h              # API giao tiếp CSDL SQLite3
│   ├── crypto_utils.h          # API SHA-256 và sinh Salt mật khẩu
│   └── client_ui.h             # Hàm giao diện client, mã màu ANSI
├── src/
│   ├── common/
│   │   ├── protocol.c          # Hàm tiện ích dùng chung (safe_copy, trim_whitespace)
│   │   └── crypto_utils.c      # Cài đặt SHA-256 (FIPS 180-2) + Salt 16 bytes
│   ├── server/
│   │   ├── main.c              # Khởi tạo Server, select() loop, Heartbeat, Graceful shutdown
│   │   ├── session.c           # Quản lý kết nối, presence, disconnect_client()
│   │   ├── router.c            # Định tuyến lệnh, chat 1-1, nhóm, xác thực DB
│   │   └── database.c          # Khởi tạo schema SQLite3, Prepared Statements, Seed mẫu
│   └── client/
│       ├── main.c              # Khởi tạo Client, select() multiplexing stdin & socket
│       ├── net_handler.c       # Gửi gói tin tới server
│       └── ui_continuous.c     # Hiển thị ANSI, xử lý input, tự động PONG
├── storage/
│   ├── chat_server.db          # File SQLite3 CSDL (tự sinh lúc chạy server)
│   └── README.md               # Mô tả thư mục lưu trữ
├── tests/
│   └── test_chatapp.py         # Script kiểm thử tự động
├── README.md                   # Hướng dẫn sử dụng
├── issue.md                    # Phân tích vấn đề mạng
├── plan_tasks.md               # Kế hoạch triển khai chi tiết
└── tasks.md                    # Danh sách theo dõi tiến độ
```

---

## 2. Hướng dẫn Biên dịch và Chạy

### Biên dịch:
Chạy lệnh `make` tại thư mục `chat-app`:
```bash
cd chat-app
make
```

Để làm sạch file thực thi:
```bash
make clean
```

### Chạy Server:
Cú pháp:
```bash
./server [port]
```
Ví dụ:
```bash
./server 8080
```
*(Nếu không truyền port, server mặc định lắng nghe trên cổng 8080).*
*(Nhấn Ctrl+C để thực hiện graceful shutdown – server sẽ thông báo cho tất cả client trước khi đóng.)*

### Chạy Client:
Cú pháp:
```bash
./client [server_ip] [port]
```
Ví dụ kết nối trên localhost:
```bash
./client 127.0.0.1 8080
```

---

## 3. Hướng dẫn Sử dụng Lệnh Phía Client

Client hỗ trợ giao diện dòng lệnh trực quan với màu sắc ANSI để dễ dàng phân biệt tin nhắn riêng, tin nhóm và thông báo hệ thống.

### Khi chưa đăng nhập:
- `/register <tên_đăng_nhập> <mật_khẩu>`: Đăng ký tài khoản mới.
- `/login <tên_đăng_nhập> <mật_khẩu>`: Đăng nhập vào hệ thống.
  *(Các tài khoản mẫu có sẵn: `alice`, `bob`, `charlie`, `david` với mật khẩu `123456`).*
- `/help`: Xem trợ giúp.
- `/quit`: Thoát ứng dụng.

### Sau khi đăng nhập thành công:
- `/online` hoặc `/users`: Xem danh sách tất cả những người đang online.
- `/msg <username> <nội_dung>`: Gửi tin nhắn riêng 1-1 cho một người.
- `/chat <username>`: Bắt đầu phiên chat 1-1 với người đó (từ lúc này, mọi nội dung bạn gõ sẽ tự động gửi thẳng cho đối phương).
- `/endchat`: Kết thúc phiên chat 1-1 và quay lại chế độ bình thường.
- `/join <tên_nhóm>`: Tham gia vào một nhóm chat (tự động tạo nhóm nếu nhóm chưa tồn tại).
- `/leave <tên_nhóm>`: Rời khỏi nhóm chat.
- `/group <tên_nhóm> <nội_dung>`: Gửi tin nhắn tới toàn bộ thành viên trong nhóm.
- `/groups`: Xem danh sách các nhóm đang có trên server.
- `/members <tên_nhóm>`: Xem danh sách thành viên trong nhóm cụ thể.
- `/logout`: Đăng xuất tài khoản hiện tại.
- `/help`: Hiển thị lại bảng hướng dẫn lệnh.
- `/quit`: Thoát chương trình.

---

## 4. Tính năng Heartbeat (PING/PONG)

Server tự động gửi `PING` tới các client không hoạt động sau **15 giây**. Client tự động phản hồi `PONG`. Nếu client không phản hồi trong **45 giây**, server tự ngắt kết nối (giải quyết Half-Open Connection).

## 5. Graceful Shutdown

Nhấn `Ctrl+C` trên server sẽ:
1. Gửi thông báo `SERVER_SHUTDOWN` tới tất cả client đang kết nối.
2. Đóng tất cả socket và giải phóng tài nguyên.
3. Thoát chương trình an toàn.

---

## 6. Chạy Kiểm Thử Tự Động (Automated Testing)

Một kịch bản kiểm thử tích hợp tự động toàn diện được cung cấp sẵn bằng Python:
```bash
python3 tests/test_chatapp.py
```
Kịch bản tự động kiểm tra:
1. Thông điệp chào mừng kết nối.
2. Từ chối đăng nhập sai mật khẩu.
3. Đăng nhập thành công và đẩy danh sách online.
4. Đăng ký tài khoản mới và từ chối đăng nhập trùng lặp (duplicate login).
5. Phát hiện và phát sự kiện trạng thái (online / offline presence) theo thời gian thực.
6. Chuyển tiếp tin nhắn riêng 1-1.
7. Tham gia nhóm và phát tán tin nhắn nhóm.
8. Mở phiên chat 1-1 và phát thông báo khi đối phương ngắt kết nối đột ngột.