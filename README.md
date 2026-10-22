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
- **[Mới - M3]** Quản lý hiện diện thời gian thực (Presence Push): Phát sóng tự động trạng thái đăng nhập/rời mạng (`EVENT_ONLINE`, `EVENT_OFFLINE`).
- **[Mới - M3]** Lưu trữ tin nhắn 1-1 và tin nhóm bền vững trong bảng `messages` SQLite3.
- **[Mới - M3]** Đồng bộ lịch sử tin nhắn: Tự động tải tin nhắn gần nhất khi mở phiên chat 1-1 (`/chat`), vào nhóm (`/join`) hoặc qua lệnh `/history`.
- **[Mới - M3]** Hàng đợi tin nhắn ngoại tuyến (Offline Messages): Tự động lưu và đẩy tin nhắn chưa đọc khi người nhận online lại.
- **[Mới - M3]** Nhóm chat bền vững (Group Persistence): Lưu trữ nhóm và thành viên trong CSDL, khôi phục nguyên vẹn sau khi khởi động lại server.
- **[Mới - M4]** Truyền tải File & Ảnh nhị phân đa nhiệm không chặn (Non-blocking Chunk Streaming qua socket write multiplexing).
- **[Mới - M4]** Mã hóa Base64 cho từng chunk 2KB để bảo toàn ranh giới gói tin và chống dính gói nhị phân.
- **[Mới - M4]** Kiểm tra toàn vẹn file tự động bằng mã băm SHA-256 đối chiếu giữa 2 đầu gửi/nhận.
- **[Mới - M4]** Trình xem ảnh trực tiếp trên Terminal bằng khối màu ANSI TrueColor Half-blocks (`▀`).

---

## 1. Cấu trúc thư mục (Modular)

```
chat-app/
├── Makefile                    # Biên dịch đa module (-Wall -Wextra -Werror -g -O2 -lsqlite3 -lpthread)
├── include/
│   ├── protocol.h              # Định nghĩa lệnh, mã lỗi, hằng số giao thức
│   ├── server.h                # Cấu trúc ClientSession, Group, FileTransferSession
│   ├── database.h              # API giao tiếp CSDL SQLite3
│   ├── crypto_utils.h          # API SHA-256, sinh Salt mật khẩu và Base64
│   └── client_ui.h             # Hàm giao diện client, mã màu ANSI, File Jobs
├── src/
│   ├── common/
│   │   ├── protocol.c          # Hàm tiện ích dùng chung (safe_copy, trim_whitespace)
│   │   └── crypto_utils.c      # Cài đặt SHA-256 (FIPS 180-2), Salt và Base64
│   ├── server/
│   │   ├── main.c              # Khởi tạo Server, select() loop, Heartbeat, Graceful shutdown
│   │   ├── session.c           # Quản lý kết nối, presence, disconnect_client()
│   │   ├── router.c            # Định tuyến lệnh, chat 1-1, nhóm, xác thực DB
│   │   ├── database.c          # Khởi tạo schema SQLite3, Prepared Statements, Seed mẫu
│   │   └── file_transfer.c     # Điều phối truyền file/ảnh theo chunk, ghi DB
│   └── client/
│       ├── main.c              # Khởi tạo Client, select() non-blocking read/write
│       ├── net_handler.c       # Gửi/nhận chunk file nền, xác minh SHA-256, xem ảnh
│       └── ui_continuous.c     # Hiển thị ANSI, xử lý input, tự động PONG
├── storage/
│   ├── chat_server.db          # File SQLite3 CSDL (tự sinh lúc chạy server)
│   ├── server_uploads/         # Thư mục đệm file/ảnh trên server
│   └── client_downloads/       # Thư mục lưu file/ảnh tải về phía client
├── tests/
│   └── test_chatapp.py         # Script kiểm thử tự động toàn diện
├── README.md                   # Hướng dẫn sử dụng
├── issue.md                    # Phân tích vấn đề mạng
├── plan_tasks.md               # Kế hoạch triển khai chi tiết
└── tasks.md                    # Danh sách theo dõi tiến độ
```
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
./client [server_ip] [port] [--raw | --simple-cli]
```
- **Chế độ Mặc định (Continuous TUI):** Tự động kích hoạt giao diện TUI chia khung chuyên nghiệp khi chạy trong Terminal (Khung chat 75%, Sidebar online/nhóm 25%, Status bar hiển thị tiến trình gửi nhận file, Input Box độc lập không bị trôi chữ khi có tin nhắn mới). Hỗ trợ phím Tab tự điền lệnh/username, mũi tên trái/phải, phím Delete, PgUp/PgDn cuộn tin nhắn.
- **Chế độ CLI thuần (`--raw` hoặc `--simple-cli`):** Chạy ở chế độ dòng lệnh chuẩn đơn giản, hữu ích khi chạy pipe, test tự động bằng script hoặc terminal hạn chế.

Ví dụ kết nối trên localhost:
```bash
./client 127.0.0.1 8080
```
Hoặc chạy chế độ đơn giản:
```bash
./client 127.0.0.1 8080 --raw
```

---

## 3. Hướng dẫn Sử dụng Lệnh Phía Client

Client hỗ trợ giao diện phân chia khung liên tục (Continuous TUI) với màu sắc ANSI TrueColor để dễ dàng phân biệt tin nhắn riêng, tin nhóm, ảnh thumbnail và thông báo hệ thống.

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
- `/sendfile <username> <đường_dẫn_file>`: Gửi tập tin bất kỳ cho người dùng khác (hỗ trợ file tới 50MB, cắt chunk 2KB Base64 non-blocking, kiểm tra toàn vẹn SHA-256).
- `/sendimg <username> <đường_dẫn_ảnh>`: Gửi hình ảnh (.bmp, .png, .jpg) cho người dùng khác với cờ hiển thị thumbnail trực tiếp trên Terminal.
- `/history <user/nhóm> [số_lượng]`: Xem lịch sử tin nhắn lưu trữ từ CSDL SQLite3 (mặc định 15 tin nhắn).
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

Bộ kiểm thử tích hợp tự động toàn diện v2.0 với 20 kịch bản kiểm thử:
```bash
python3 tests/test_chatapp_v2.py
```
Kịch bản tự động kiểm tra:
1. Thông điệp chào mừng kết nối ban đầu.
2. Từ chối thực thi lệnh khi chưa xác thực (Unauthenticated command guard).
3. Từ chối đăng nhập sai mật khẩu.
4. Đăng nhập thành công với tài khoản mẫu SQLite3 (SHA-256 + Salt).
5. Từ chối đăng nhập lại trên cùng một phiên và từ chối đăng nhập trùng (Duplicate login).
6. Đăng ký tài khoản mới và từ chối đăng ký trùng tên.
7. **Tính bền vững của SQLite3 qua các lần khởi động lại server (Persistence Test)**: Đăng ký tài khoản mới, tắt server, bật lại server và đăng nhập thành công.
8. Phát hiện và phát sự kiện trạng thái (online / offline presence) theo thời gian thực (`EVENT_ONLINE`, `EVENT_OFFLINE`).
9. Chặn tự gửi tin nhắn cho chính mình (`Cannot send message to yourself`).
10. Chuyển tiếp tin nhắn riêng 1-1 và tự động lưu vào bảng `messages` CSDL.
11. Tham gia nhóm, rời nhóm, chặn gửi tin vào nhóm khi chưa join và phát tán tin nhắn nhóm đồng bộ CSDL.
12. Mở phiên chat 1-1 (`START_CHAT`) và phát thông báo khi đối phương ngắt kết nối đột ngột.
13. Tự động đồng bộ và truy vấn lịch sử tin nhắn 1-1 và nhóm (`HISTORY`).
14. Hàng đợi tin nhắn ngoại tuyến (Offline messages queue): Lưu tin khi người nhận offline và tự động đẩy về khi online lại (`OFFLINE_MSG`).
15. Cơ chế nhịp tim giữ kết nối (Heartbeat PING/PONG).
16. **Hiện tượng dính gói TCP (Sticky Packet Stress Test)**: Bắn liên tiếp 20 lệnh trong 1 buffer TCP duy nhất để xác nhận bộ phân tách framing `\n` hoạt động chuẩn 100%.
17. Từ chối khởi tạo truyền file vượt quá giới hạn 50MB (`FILE_REJECTED`).
18. Truyền file nhị phân hoàn chỉnh theo chunk Base64 non-blocking và đối chiếu checksum SHA-256.
19. Truyền file hình ảnh với cờ nhận diện `is_image = 1` và hiển thị trên terminal người nhận.
20. Tự động thu hồi và thông báo hủy phiên truyền file mồ côi khi một bên ngắt kết nối đột ngột.

---

## 7. Kiểm Tra Chịu Tải & Rò Rỉ Tài Nguyên (Stress Test & Valgrind)

### A. Kiểm thử chịu tải đồng thời (Stress Test):
Chạy script giả lập 50 - 100 client luồng đồng thời:
```bash
python3 tests/stress_test.py
```
- Kết nối đồng thời 50 clients.
- Đăng ký tài khoản, đăng nhập, tham gia phòng `stress_room`.
- Bắn liên tục hàng trăm tin nhắn nhóm.
- 50% client ngắt kết nối đột ngột (drop socket không báo trước).
- Kiểm tra server không bị crash bởi tín hiệu `SIGPIPE`, không bị deadlock và vẫn phục vụ các client còn lại bình thường.

### B. Kiểm tra rò rỉ bộ nhớ (Valgrind Memcheck):
Chạy kịch bản phân tích rò rỉ bộ nhớ và tài nguyên File Descriptor:
```bash
chmod +x scripts/run_valgrind.sh
./scripts/run_valgrind.sh
```
Script sẽ khởi động server dưới sự giám sát của Valgrind (`--leak-check=full --track-fds=yes`), thực hiện các bài test tải và gửi tín hiệu Graceful Shutdown để xác nhận:
- `0 errors from 0 contexts`
- Không rò rỉ con trỏ `malloc` / `free`.
- Không rò rỉ socket và con trỏ file (`sqlite3_finalize`, `fclose`, `close`).