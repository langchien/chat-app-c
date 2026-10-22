# C Chat Application (v2.0)

Ứng dụng nhắn tin Client - Server hướng console viết bằng ngôn ngữ C (C99/C11). Hỗ trợ kiến trúc đa phiên non-blocking qua `select()`, lưu trữ CSDL nhúng SQLite3, truyền tải file/ảnh nhị phân và giao diện Continuous TUI phân chia khung với màu sắc ANSI.

---

## Tính năng nổi bật

- **Kết nối mạng & Đa phiên:** Mô hình I/O Multiplexing bằng `select()`, phục vụ đồng thời nhiều client, xử lý chống dính gói TCP framing (`\n`), chống rò rỉ socket và chặn tín hiệu `SIGPIPE`.
- **Nhắn tin linh hoạt:** Chat riêng tư 1-1 (`/msg`, `/chat`), chat phòng nhóm (`/join`, `/group`), tra cứu lịch sử tin nhắn (`/history`).
- **Hiện diện & Hàng đợi Offline:** Tự động phát sóng trạng thái online/offline thời gian thực (`EVENT_ONLINE`, `EVENT_OFFLINE`), lưu trữ tin nhắn khi đối phương offline vào SQLite3 và tự động đẩy tin khi người nhận online lại.
- **Bảo mật & CSDL SQLite3:** Quản lý tài khoản, nhóm, tin nhắn và lịch sử truyền file qua SQLite3 (`storage/chat_server.db`). Mật khẩu được băm SHA-256 kết hợp Salt 16 bytes ngẫu nhiên, 100% Prepared Statements chống SQL Injection.
- **Truyền File & Ảnh nhị phân:** Cắt chunk 2KB mã hóa Base64 non-blocking, đối chiếu mã băm SHA-256 để kiểm tra toàn vẹn, hiển thị trực tiếp ảnh trên Terminal bằng ANSI TrueColor.
- **Giao diện Console thông minh (Continuous TUI):** Chia layout độc lập (khung tin nhắn, danh sách online/nhóm, thanh trạng thái, ô nhập lệnh độc lập chống trôi chữ), hỗ trợ phím Tab tự hoàn thiện lệnh.
- **Độ tin cậy cao:** Cơ chế Heartbeat (PING/PONG) phát hiện half-open connection và Graceful Shutdown (bắt `SIGINT`) đóng tất cả kết nối an toàn.

---

## Cấu trúc thư mục

```text
chat-app-c/
├── Makefile               # Biên dịch toàn bộ dự án (-Wall -Wextra -Werror -O2)
├── include/               # Header files (protocol, server, database, crypto, UI)
├── src/
│   ├── common/            # Protocol framing, tiện ích SHA-256 & Base64
│   ├── server/            # Socket loop, session manager, router, database, file transfer
│   └── client/            # Client loop, bộ xử lý gói tin, continuous TUI
├── storage/               # File CSDL SQLite3 và thư mục tải file
├── tests/                 # Kịch bản kiểm thử tự động bằng Python (20 bài test tích hợp)
└── docs/                  # Tài liệu chi tiết (getting-started.md, ...)
```

---

## Khởi động nhanh (Quick Start)

> Xem hướng dẫn chi tiết từ lúc clone code đến cài đặt trên Linux/Windows tại [docs/getting-started.md](docs/getting-started.md).

```bash
# 1. Cài đặt thư viện phụ thuộc (Ubuntu/Debian)
sudo apt update && sudo apt install -y build-essential libsqlite3-dev python3

# 2. Biên dịch mã nguồn
make clean && make

# 3. Khởi động Server (Terminal 1)
./server 8080

# 4. Khởi động Client (Terminal 2 & 3)
./client 127.0.0.1 8080
```

---

## Bảng lệnh Client

| Lệnh | Cú pháp | Mô tả |
| :--- | :--- | :--- |
| **Đăng ký** | `/register <user> <pass>` | Đăng ký tài khoản người dùng mới |
| **Đăng nhập** | `/login <user> <pass>` | Đăng nhập *(tài khoản mẫu: `alice`, `bob`, `charlie`, `david` / pass: `123456`)* |
| **Hiện diện** | `/online` hoặc `/users` | Xem danh sách người dùng đang online |
| **Tin nhắn riêng** | `/msg <user> <nội dung>` | Gửi tin nhắn riêng 1-1 cho một người |
| **Phiên chat 1-1** | `/chat <user>` | Vào chế độ chat trực tiếp 1-1 (thoát bằng `/endchat`) |
| **Nhóm chat** | `/join <nhóm>` | Tham gia hoặc tạo nhóm chat mới |
| **Nhắn tin nhóm** | `/group <nhóm> <nội dung>` | Gửi tin nhắn đến toàn bộ thành viên trong nhóm |
| **Thông tin nhóm** | `/groups` / `/members <nhóm>` | Xem danh sách nhóm / thành viên của một nhóm |
| **Gửi tập tin** | `/sendfile <user> <đường dẫn>` | Gửi file (tối đa 50MB, kiểm tra mã SHA-256) |
| **Gửi hình ảnh** | `/sendimg <user> <đường dẫn>` | Gửi ảnh và hiển thị ảnh trên Terminal người nhận |
| **Lịch sử chat** | `/history <user/nhóm> [số lượng]` | Xem lại lịch sử tin nhắn từ CSDL SQLite3 |
| **Đăng xuất / Thoát**| `/logout` / `/quit` | Đăng xuất phiên hiện tại / Thoát ứng dụng |

---

## Kiểm thử tự động (Testing)

Chạy bộ kiểm thử tự động toàn diện 20 kịch bản:
```bash
python3 tests/test_chatapp_v2.py
```

Kiểm thử chịu tải đồng thời (50 clients ngắt kết nối ngẫu nhiên):
```bash
python3 tests/stress_test.py
```

---

## Tài liệu chi tiết

- [Hướng dẫn thiết lập từ A-Z (Getting Started)](docs/getting-started.md)
- [Phân tích sự cố mạng & Giải pháp kiến trúc](issue.md)
- [Kế hoạch chi tiết các mốc phát triển (Plan Tasks)](plan_tasks.md)