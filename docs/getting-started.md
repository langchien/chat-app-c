# Hướng Dẫn Thiết Lập & Bắt Đầu (Getting Started)

Tài liệu này hướng dẫn chi tiết từng bước từ lúc clone mã nguồn về máy, chuẩn bị môi trường, biên dịch cho đến khi khởi chạy và thử nghiệm các tính năng của ứng dụng **Chat App C (v2.0)**.

---

## 1. Yêu cầu Hệ thống & Cài đặt Môi trường (Prerequisites)

Dự án được phát triển bằng ngôn ngữ **C (C99/C11)** với kiến trúc POSIX Socket (`<sys/socket.h>`, `select()`, `<signal.h>`), thư viện đa luồng `pthread`, cơ sở dữ liệu **SQLite3** và kịch bản test bằng **Python 3**.

### Trên Ubuntu / Debian / Linux Mint:
```bash
sudo apt update
sudo apt install -y build-essential libsqlite3-dev python3 git
```
*Gói cài đặt:*
- `build-essential`: Bao gồm `gcc`, `make` và các công cụ biên dịch tiêu chuẩn.
- `libsqlite3-dev`: Header `<sqlite3.h>` và thư viện liên kết SQLite3 (bắt buộc cho module CSDL).
- `python3`: Chạy các bộ kịch bản kiểm thử tự động.
- `git`: Công cụ quản lý mã nguồn.

### Trên Fedora / RHEL / CentOS:
```bash
sudo dnf install -y gcc make sqlite-devel python3 git
```

### Trên Arch Linux / Manjaro:
```bash
sudo pacman -Syu --needed base-devel sqlite python git
```

### Dành cho người dùng Windows:
Ứng dụng sử dụng các hàm API mạng POSIX tiêu chuẩn của Linux. Nếu bạn đang dùng hệ điều hành Windows:
1. Mở **PowerShell (Administrator)** và cài đặt WSL2 (Ubuntu):
   ```powershell
   wsl --install -d Ubuntu
   ```
2. Khởi động lại máy (nếu được yêu cầu), sau đó mở ứng dụng **Ubuntu** từ Start Menu.
3. Chạy lệnh cài đặt của Ubuntu ở trên (`sudo apt update && sudo apt install -y build-essential libsqlite3-dev python3 git`).
4. Truy cập vào thư mục mã nguồn dự án thông qua đường dẫn mount của WSL (ví dụ: `/mnt/p/C++/chat-app-c` nếu source code nằm ở ổ `P:`).

---

## 2. Tải Mã Nguồn (Git Clone)

Mở terminal và clone repository về máy:

```bash
git clone https://github.com/langchien/chat-app-c.git
cd chat-app-c
```

---

## 3. Cấu trúc Thư mục

Trước khi biên dịch, hãy kiểm tra các thư mục cốt lõi của dự án:

```text
chat-app-c/
├── Makefile                # File cấu hình biên dịch tự động
├── include/                # Các tệp header (.h)
│   ├── client_ui.h         # Giao diện Continuous TUI & mã màu ANSI
│   ├── crypto_utils.h      # Mã hóa SHA-256, sinh Salt & Base64
│   ├── database.h          # API tương tác SQLite3
│   ├── protocol.h          # Định nghĩa lệnh, hằng số giao thức
│   └── server.h            # Cấu trúc session, group, file transfer
├── src/                    # Mã nguồn C (.c)
│   ├── common/             # Protocol parser & Crypto utils
│   ├── server/             # Server loop, session, router, database, file transfer
│   └── client/             # Client loop, network handler, continuous TUI
├── storage/                # Thư mục lưu DB SQLite3 và file đệm tải về
├── tests/                  # Kịch bản kiểm thử tích hợp tự động
└── docs/                   # Tài liệu hướng dẫn
```

*Lưu ý:* Thư mục `storage/` sẽ được server và client tự động tạo nếu chưa có. Tuy nhiên, bạn cũng có thể tạo thủ công trước:
```bash
mkdir -p storage
```

---

## 4. Biên dịch Mã Nguồn (Build)

Dự án được biên dịch thông qua `Makefile` với các cờ kiểm soát chất lượng code nghiêm ngặt (`-Wall -Wextra -Werror -g -O2`).

### Lệnh biên dịch:
```bash
make clean && make
```

### Kết quả đầu ra:
Sau khi biên dịch thành công, hai tệp thực thi sẽ xuất hiện ở thư mục gốc:
- `server`: Chương trình máy chủ điều phối tin nhắn và CSDL.
- `client`: Chương trình giao diện người dùng (Continuous TUI).

*(Nếu muốn biên dịch riêng từng phần: `make server` hoặc `make client`).*

---

## 5. Khởi chạy Ứng dụng

Để thử nghiệm tính năng nhắn tin đầy đủ, bạn cần mở **ít nhất 2 hoặc 3 cửa sổ terminal**:
- 1 terminal chạy **Server**.
- 2 terminal chạy **Client** đại diện cho 2 người dùng khác nhau.

---

### Bước 1: Khởi động Server (Terminal 1)

Chạy lệnh sau tại thư mục gốc của dự án:

```bash
./server 8080
```

> **Lưu ý:**
> - Nếu không chỉ định cổng, server mặc định lắng nghe trên cổng `8080`.
> - Khi khởi chạy lần đầu tiên, server tự động khởi tạo file CSDL `storage/chat_server.db` và nạp sẵn 4 tài khoản mẫu:
>   - **Tài khoản:** `alice`, `bob`, `charlie`, `david`
>   - **Mật khẩu chung:** `123456`
> - Để tắt server an toàn, nhấn `Ctrl + C`. Server sẽ kích hoạt cơ chế **Graceful Shutdown** (thông báo đóng phiên cho mọi client đang online rồi mới thoát).

---

### Bước 2: Khởi động Client 1 (Terminal 2)

Mở cửa sổ terminal thứ hai và kết nối vào server:

```bash
./client 127.0.0.1 8080
```

- Mặc định, client sẽ khởi chạy với giao diện **Continuous TUI** (chia khung hiển thị tin nhắn, danh sách online và ô nhập lệnh độc lập, không bị trôi con trỏ khi có tin nhắn mới tới).
- *(Tùy chọn)* Nếu chạy trong môi trường terminal không hỗ trợ mã màu ANSI hoặc muốn chạy chế độ dòng lệnh chuẩn, thêm cờ `--raw`:
  ```bash
  ./client 127.0.0.1 8080 --raw
  ```

---

### Bước 3: Khởi động Client 2 (Terminal 3)

Mở thêm cửa sổ terminal thứ ba để làm người trò chuyện cùng:

```bash
./client 127.0.0.1 8080
```

---

## 6. Kịch bản Trải nghiệm Thực tế (Hands-on Walkthrough)

Sau khi cả 2 client kết nối tới server, hãy làm theo kịch bản sau để kiểm tra toàn bộ tính năng:

### 1. Đăng nhập tài khoản
- Tại **Terminal 2 (Client 1)**, nhập:
  ```text
  /login alice 123456
  ```
- Tại **Terminal 3 (Client 2)**, nhập:
  ```text
  /login bob 123456
  ```
- **Kết quả:** Cả hai nhận được thông báo sự kiện hiện diện: Alice thấy Bob online và Bob thấy Alice online.

*(Hoặc đăng ký tài khoản hoàn toàn mới: `/register user1 password123` rồi `/login user1 password123`).*

### 2. Xem danh sách người đang online
- Gõ:
  ```text
  /online
  ```
- Hệ thống sẽ liệt kê các người dùng đang có kết nối tới server.

### 3. Gửi tin nhắn riêng (1-1)
- Từ phía Alice, gửi tin cho Bob:
  ```text
  /msg bob Xin chào Bob, bạn có khỏe không?
  ```
- Bob sẽ nhận được tin nhắn màu nổi bật hiển thị rõ tên người gửi `[alice -> bạn]`.

### 4. Vào phiên chat trực tiếp (Direct Chat Mode)
Thay vì phải gõ `/msg bob ...` mỗi lần nhắn, bạn có thể chuyển hẳn sang chế độ chat trực tiếp:
- Alice gõ:
  ```text
  /chat bob
  ```
- Từ lúc này, Alice chỉ cần gõ nội dung bình thường và nhấn Enter, tin nhắn sẽ tự động gửi thẳng cho Bob.
- Để thoát chế độ này, gõ:
  ```text
  /endchat
  ```

### 5. Tạo và tham gia phòng chat nhóm (Group Chat)
- Alice tham gia phòng nhóm:
  ```text
  /join dev-team
  ```
- Bob cũng tham gia phòng nhóm đó:
  ```text
  /join dev-team
  ```
- Gửi tin nhắn vào nhóm:
  ```text
  /group dev-team Xin chào mọi người trong nhóm dev-team!
  ```
- Cả hai đều sẽ thấy tin nhắn nhóm. Để xem thành viên trong nhóm: `/members dev-team`.

### 6. Gửi tập tin & hình ảnh
- Alice gửi một file cho Bob:
  ```text
  /sendfile bob docs/getting-started.md
  ```
- File sẽ được chia nhỏ thành các chunk 2KB mã hóa Base64 và chuyển tiếp non-blocking. Phía Bob sẽ tự động nhận, xác thực mã băm SHA-256 và lưu vào thư mục `storage/client_downloads/`.
- Nếu gửi file ảnh (`.png`, `.jpg`, `.bmp`):
  ```text
  /sendimg bob avatar.png
  ```
- Phía nhận sẽ hiển thị ảnh trực tiếp dưới dạng ANSI TrueColor ngay trên terminal.

### 7. Tra cứu lịch sử tin nhắn
- Tin nhắn 1-1 và tin nhóm đều được lưu bền vững vào SQLite3. Để xem lại:
  ```text
  /history bob 10
  /history dev-team 10
  ```

### 8. Đăng xuất hoặc thoát
- Đăng xuất tài khoản:
  ```text
  /logout
  ```
- Thoát hẳn ứng dụng:
  ```text
  /quit
  ```

---

## 7. Chạy Kiểm Thử Tự Động (Automated Testing)

Dự án tích hợp bộ kiểm thử tự động toàn diện bằng Python (chạy độc lập, tự khởi động server test và giả lập nhiều client):

### Chạy bộ 20 kịch bản kiểm thử chức năng:
```bash
python3 tests/test_chatapp_v2.py
```
*Nội dung kiểm tra:*
- Xác thực đăng nhập, từ chối mật khẩu sai, chặn đăng nhập trùng lặp.
- Đồng bộ hiện diện (online/offline) thời gian thực.
- Kiểm tra tính bền vững của SQLite3 qua các lần khởi động lại server.
- Khả năng xử lý dính gói TCP (Sticky Packet) với bộ đệm `\n` framing.
- Truyền file nhị phân Base64 kèm đối chiếu SHA-256 checksum.
- Cơ chế gửi tin offline và đẩy tin khi đối phương đăng nhập lại.

### Chạy kiểm thử chịu tải đồng thời (Stress Test):
```bash
python3 tests/stress_test.py
```
Giả lập 50 clients kết nối đồng thời, gửi tin nhắn liên tục và đột ngột drop kết nối để kiểm tra độ ổn định, chống crash bởi `SIGPIPE` và chống rò rỉ socket/bộ nhớ.

---

## 8. Xử lý Sự cố Thường gặp (Troubleshooting)

| Sự cố | Nguyên nhân | Cách khắc phục |
| :--- | :--- | :--- |
| **`make: gcc: Command not found`** | Máy chưa cài trình biên dịch C | Chạy `sudo apt install -y build-essential` |
| **`fatal error: sqlite3.h: No such file`** | Thiếu gói thư viện phát triển của SQLite3 | Chạy `sudo apt install -y libsqlite3-dev` |
| **`error: unknown type name 'sig_atomic_t'`** | File header thiếu `<signal.h>` | Đã được khắc phục trong [include/server.h](file:///p:/C++/chat-app-c/include/server.h). Chạy `make clean && make` để biên dịch lại. |
| **`bind error: Address already in use`** | Cổng 8080 đang bị chiếm dụng bởi tiến trình khác | Chạy server trên port khác: `./server 9090` (client kết nối qua `./client 127.0.0.1 9090`), hoặc tắt tiến trình cũ: `fuser -k 8080/tcp` |
| **`The term 'make' is not recognized` (Windows)** | Bạn đang chạy trực tiếp trên PowerShell Windows thay vì môi trường Linux | Cài đặt WSL2 Ubuntu (`wsl --install -d Ubuntu`) và thực hiện các thao tác bên trong terminal Ubuntu. |
| **`Permission denied: ./server`** | File chưa được cấp quyền thực thi | Chạy `chmod +x server client` |
