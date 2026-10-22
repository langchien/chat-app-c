# Kế Hoạch Triển Khai Chat C
Lưu ý code sẽ chạy trên linux!!!
> **Nguồn tài liệu:** [Gemini App](https://gemini.google.com/app/4e92fd95b2e789a4)  
> **Yêu cầu:** Viết một kế hoạch chi tiết đầy đủ và chia ra các task để triển khai ứng dụng chat như trong file readme có thêm các phần như Giao diện liên tục, gửi file, ảnh, xác thực database và các phần quan trọng khác nữa, đặc biệt phần mạng phải viết bằng lập trình C.

---

Bản kế hoạch triển khai chi tiết (**Master Implementation Plan**) và phân rã công việc (**Work Breakdown Structure - WBS**) dưới đây nhằm phát triển hệ thống ứng dụng Chat nâng cao bằng ngôn ngữ C (POSIX Socket).

Bản kế hoạch này kế thừa toàn bộ yêu cầu cốt lõi và các cơ chế xử lý lỗi tầng mạng trong `README.md` và `issue.md`, đồng thời tích hợp thêm các tính năng nâng cao:
* **Cơ sở dữ liệu thực tế:** SQLite3 + Băm mật khẩu (SHA-256 + Salt).
* **Truyền tải File/Ảnh nhị phân:** Chunking + Checksum SHA-256 + xem ảnh trực tiếp trên terminal.
* **Giao diện liên tục không vỡ dòng:** TUI Ncurses / Non-canonical ANSI Raw mode.
* **Lưu trữ lịch sử & Tin nhắn Offline:** Đồng bộ tin nhắn khi client online lại.
* **Độ tin cậy & Ổn định:** Heartbeat/Keep-alive, xử lý Half-open connection, Graceful shutdown.

---

## 1. Kiến Trúc Tổng Thể & Cấu Trúc Thư Mục Nâng Cấp

Để dễ bảo trì, mở rộng và tách biệt rõ ràng giữa **Tầng Mạng** (C Socket), **Tầng Lưu Trữ** (Database) và **Tầng Hiển Thị** (UI), cấu trúc dự án được thiết kế theo dạng module hóa:

```text
chat-app/
├── Makefile                    # Biên dịch đa module với cờ -lsqlite3, -lncurses, -lpthread, -lcrypto
├── include/
│   ├── protocol.h              # Định nghĩa lệnh, mã lỗi, cấu trúc gói tin Text & Binary Chunk
│   ├── server.h                # Cấu trúc ClientSession, Group, FileTransferState
│   ├── database.h              # API giao tiếp SQLite3 (Auth, History, Offline Msg, Files)
│   ├── crypto_utils.h          # Hàm băm SHA-256 + Salt cho mật khẩu và kiểm tra toàn vẹn file
│   └── client_ui.h             # Quản lý giao diện liên tục (Split-screen TUI / ANSI Non-blocking)
├── src/
│   ├── common/
│   │   ├── protocol.c          # Parser phân tách gói tin (xử lý Sticky Packet & Fragmentation)
│   │   └── crypto_utils.c      # Cài đặt SHA-256 / CRC32
│   ├── server/
│   │   ├── main.c              # Khởi tạo Server, Signal Handler (SIGPIPE, SIGINT), vòng lặp select()
│   │   ├── session.c           # Quản lý kết nối, Presence, Heartbeat timeout
│   │   ├── router.c            # Định tuyến chat 1-1, Group chat, thông báo ngắt kết nối
│   │   ├── file_transfer.c     # Xử lý luồng nhận/chuyển tiếp chunk file & ảnh
│   │   └── database.c          # Truy vấn SQLite3 với Prepared Statements
│   └── client/
│       ├── main.c              # Khởi tạo Client, select() giữa STDIN_FILENO và sockfd
│       ├── net_handler.c       # Gửi/nhận gói tin, tự động đóng/mở chunk file ở nền
│       └── ui_continuous.c     # Giao diện liên tục: Khung chat, Sidebar Online, Thanh tiến trình
├── storage/
│   ├── chat_server.db          # File cơ sở dữ liệu SQLite3 (thay thế hoàn toàn users.txt)
│   ├── server_uploads/         # Thư mục lưu đệm file/ảnh trên server (nếu gửi offline/group)
│   └── client_downloads/       # Thư mục lưu file/ảnh tải về ở phía client
├── tests/
│   └── test_chatapp_v2.py      # Script kiểm thử tự động toàn diện (Auth, Chat, File, Edge cases)
├── README.md
└── issue.md
```

---

## 2. Thiết Kế Các Phân Hệ Nâng Cao

### 2.1. Tầng Mạng C (Core C Networking & Hybrid Framing)
* **Kế thừa từ `issue.md`:**
  * Bỏ qua tín hiệu `SIGPIPE` (`signal(SIGPIPE, SIG_IGN)`).
  * Bật cờ `SO_REUSEADDR` để tránh lỗi `EADDRINUSE`.
  * Kiểm tra ngoại lệ `errno == EINTR` khi gọi `select()`.
  * Giới hạn `MAX_CLIENTS < FD_SETSIZE`.
* **Giao thức Lai (Hybrid Text + Binary Framing):**
  * **Các lệnh điều khiển và chat văn bản:** Giữ nguyên định dạng phân tách bằng ký tự `\n` (*Delimiter-based framing*) để tương thích hoàn toàn với thiết kế ban đầu.
  * **Khi truyền File/Ảnh:** Sử dụng *Chunk-based Framing* (mã hóa Base64 trên từng chunk 4KB hoặc Header độ dài `FILE_CHUNK <transfer_id> <seq> <byte_len>\n<raw_bytes>`) để không làm tắc nghẽn (*block*) vòng lặp `select()` đơn luồng của server. Nếu gửi 1 file 10MB trong 1 lần `send()`, toàn bộ các client khác sẽ bị treo; chia nhỏ thành các chunk giúp server xen kẽ xử lý tin nhắn văn bản và truyền file mượt mà.

### 2.2. Xác Thực & Cơ Sở Dữ Liệu (Database Authentication & Persistence)
Thay thế file `users.txt` đơn giản bằng hệ quản trị CSDL nhúng SQLite3 (thư viện C chuẩn `<sqlite3.h>`):
* **Bảng `users`:** `id`, `username` (UNIQUE), `password_hash` (SHA-256), `salt`, `created_at`, `last_seen`.
* **Bảng `groups` & `group_members`:** Lưu trữ nhóm lâu dài (không bị mất khi tắt server).
* **Bảng `messages`:** Lưu lịch sử tin nhắn 1-1 và nhóm (`sender`, `receiver/group`, `content`, `msg_type` [TEXT/FILE/IMAGE], `is_delivered`, `timestamp`).
* **Tính năng Tin nhắn Offline & Lịch sử:** Khi đăng nhập hoặc mở phiên `/chat <user>`, server tự động truy vấn 20 tin nhắn gần nhất hoặc các tin nhắn gửi lúc user đang offline để đẩy về client.

### 2.3. Truyền Tải File & Hình Ảnh (File & Image Transfer)
* **Quy trình gửi/nhận:**
  1. **Khởi tạo:** Client gửi `FILE_OFFER <target_user/group> <filename> <filesize> <sha256_checksum> <type:FILE|IMG>`.
  2. **Cấp phát phiên truyền:** Server kiểm tra dung lượng (ví dụ giới hạn $\le$ 50MB), tạo `transfer_id` và thông báo cho người nhận `FILE_INCOMING <transfer_id> <sender> <filename> <size>`.
  3. **Truyền theo luồng (Streaming Chunks):** Mỗi vòng lặp `select()`, client đọc 4KB từ đĩa, gửi `FILE_DATA <transfer_id> <seq> <base64_payload>\n`.
  4. **Hoàn tất & Xác minh:** Gửi `FILE_EOF <transfer_id>`. Phía nhận tính lại mã băm SHA-256 của file vừa ghép; nếu khớp với checksum ban đầu thì báo `FILE_SUCCESS`, ngược lại báo lỗi hỏng file.
* **Hiển thị Ảnh:** Trên giao diện Terminal của Client, ngoài việc lưu file ảnh gốc vào `client_downloads/`, hỗ trợ render ảnh trực tiếp ra khung chat dưới dạng ASCII Art / ANSI TrueColor Half-blocks (`▀`, `▄`) (sử dụng thư viện header-only `stb_image.h` trong C) để xem nhanh ảnh ngay trong cửa sổ chat.

### 2.4. Giao Diện Liên Tục Không Gián Đoạn (Continuous Asynchronous UI)
* **Vấn đề của CLI thông thường:** Khi người dùng đang gõ dở một câu lệnh ở dòng nhắc (`> Xin ch...`), nếu có tin nhắn từ server gửi tới và in ra bằng `printf()`, dòng chữ đang gõ sẽ bị cắt ngang và trôi lên trên (vỡ giao diện).
* **Giải pháp C (2 cấp độ tùy chọn triển khai):**
  * **Cấp độ 1 (TUI hoàn chỉnh với `ncurses.h`):** Chia màn hình Terminal thành 4 vùng cố định:
    * *Header Bar:* Trạng thái kết nối, tên tài khoản, đối tác đang `/chat` hoặc nhóm hiện tại.
    * *Main Chat Window (Cuộn tự động):* Hiển thị tin nhắn đến/đi, lịch sử chat, preview ảnh ASCII.
    * *Sidebar (Bên phải):* Danh sách người dùng Online (`ONLINE_LIST`) và các Nhóm cập nhật thời gian thực mà không cần gõ `/online`.
    * *Status & Input Bar (Dưới cùng):* Thanh tiến trình tải file (`[████░░░░░░] 42%`) và ô nhập liệu cố định (giữ nguyên con trỏ và chuỗi đang gõ bất kể có bao nhiêu tin nhắn đổ về).
  * **Cấp độ 2 (Raw Mode `termios.h` + ANSI Escape Codes):** Tắt chế độ Canonical (`ICANON`) và `ECHO` của terminal, tự quản lý `input_buffer`. Mỗi khi socket nhận được dữ liệu từ `select()`, xóa dòng hiện tại (`\r\033[K`), in tin nhắn mới lên trên, sau đó vẽ lại chính xác dòng người dùng đang gõ (Prompt + `input_buffer`).

### 2.5. Các Phần Quan Trọng Khác (Production-grade Features)
* **Cơ chế Heartbeat (`PING` / `PONG`):** Giải quyết triệt để lỗi Half-Open Connection (mục 1.4 trong `issue.md`). Server kiểm tra định kỳ mỗi 15 giây; nếu `now - client.last_active > 45s`, tự động gọi `disconnect_client()`.
* **Trạng thái "Đang gõ..." (Typing Indicator):** Khi người dùng gõ phím trong chế độ `/chat <user>`, gửi gói tin nhẹ `TYPING_START <user>` để thanh trạng thái đối phương hiện *"alice đang nhập tin nhắn..."*.
* **Graceful Shutdown:** Bắt tín hiệu `SIGINT` (Ctrl+C) và `SIGTERM` trên Server để gửi thông báo `SERVER_SHUTDOWN`, đóng toàn bộ kết nối SQLite3, giải phóng bộ nhớ động và đóng toàn bộ FD trước khi thoát.
* **Rate Limiting (Chống Spam/DoS):** Giới hạn số lệnh/giây trên mỗi `ClientSession` để tránh một client làm ngập bộ đệm của server.

---

## 3. Phân Chia Task Chi Tiết (Work Breakdown Structure)

### Giai Đoạn 1: Nền Tảng Mạng C & Kiến Trúc Cốt Lõi (Milestone 1)
> **Mục tiêu:** Xây dựng bộ khung TCP Client-Server vững chắc bằng C, giải quyết toàn bộ vấn đề tầng Transport và I/O Multiplexing trong `issue.md`.

* **Task 1.1: Thiết lập Cấu trúc Dự án & Hệ thống Build (`Makefile`)**
  * *Công việc:* Tạo cây thư mục, viết `Makefile` hỗ trợ biên dịch từng module với các cờ cảnh báo nghiêm ngặt (`-Wall -Wextra -Werror -g -O2`).
  * *Đầu ra (Deliverable):* Lệnh `make all`, `make server`, `make client`, `make clean` hoạt động trơn tru.

* **Task 1.2: Cài đặt Core TCP Server & Quản lý Socket (`src/server/main.c`, `session.c`)**
  * *Công việc:*
    * Khởi tạo socket lắng nghe, cấu hình `SO_REUSEADDR` (tránh lỗi `EADDRINUSE`).
    * Vô hiệu hóa `SIGPIPE` (`signal(SIGPIPE, SIG_IGN)`) để tránh crash server khi client đóng kết nối đột ngột.
    * Thiết lập vòng lặp `select()`, xử lý ngoại lệ `errno == EINTR`, kiểm soát giới hạn `MAX_CLIENTS = 128` (`< FD_SETSIZE`).
    * Viết hàm `disconnect_client(int idx)` tập trung để đóng FD, giải phóng tài nguyên, tránh rò rỉ File Descriptor (FD Leak).
  * *Đầu ra:* Server chấp nhận đa kết nối đồng thời, chịu được việc client ngắt kết nối đột ngột.

* **Task 1.3: Bộ Phân Tách Gói Tin & Chống Sticky Packet (`src/common/protocol.c`)**
  * *Công việc:*
    * Thiết kế bộ đệm vòng hoặc bộ đệm trượt (`recv_buf` và `recv_len`) riêng biệt cho từng `ClientSession`.
    * Cài đặt thuật toán quét ký tự `\n` để tách từng câu lệnh hoàn chỉnh và dùng `memmove()` dịch chuyển phần dữ liệu dư (chưa đủ câu lệnh) về đầu buffer.
    * Xử lý trường hợp tràn bộ đệm (`recv_len >= BUFFER_SIZE`) bằng cách ngắt gói tin lỗi và trả về `ERROR Message too long`.
  * *Đầu ra:* Server nhận chính xác 100% câu lệnh dù client gửi gộp 10 lệnh trong 1 lần `send()` hoặc chia 1 lệnh thành 5 lần `send()`.

* **Task 1.4: Cơ chế Heartbeat & Bắt Tín Hiệu Hệ Thống (Keep-Alive & Graceful Shutdown)**
  * *Công việc:*
    * Sử dụng tham số `struct timeval timeout` trong hàm `select()` (chu kỳ 5 giây).
    * Cập nhật trường `time_t last_active` mỗi khi nhận dữ liệu từ client. Tự động gửi `PING` và ngắt kết nối nếu quá hạn không nhận được `PONG`.
    * Bắt tín hiệu `SIGINT` để đóng DB và giải phóng toàn bộ socket khi tắt server.

---

### Giai Đoạn 2: Xác Thực Cơ Sở Dữ Liệu & Bảo Mật (Milestone 2)
> **Mục tiêu:** Thay thế hoàn toàn `users.txt` bằng CSDL SQLite3, mã hóa mật khẩu và quản lý phiên đăng nhập an toàn.

* **Task 2.1: Thiết kế Schema & Tích hợp SQLite3 C API (`src/server/database.c`)**
  * *Công việc:*
    * Viết hàm `db_init("storage/chat_server.db")` tự động khởi tạo các bảng: `users`, `groups`, `group_members`, `messages`, `file_transfers`.
    * Bật chế độ `PRAGMA journal_mode=WAL;` trong SQLite để tối ưu tốc độ đọc/ghi đồng thời.
    * Viết hàm khởi tạo sẵn các tài khoản mẫu (`alice`, `bob`, `charlie`, `david` - mật khẩu `123456`) nếu DB mới tạo lần đầu.

* **Task 2.2: Băm Mật Khẩu & Chống SQL Injection (`src/common/crypto_utils.c`)**
  * *Công việc:*
    * Cài đặt hàm sinh chuỗi salt ngẫu nhiên (16 bytes) và băm SHA-256(`password + salt`).
    * Sử dụng 100% `sqlite3_prepare_v2()` và `sqlite3_bind_text()` cho mọi truy vấn SQL, tuyệt đối không nối chuỗi bằng `sprintf` để loại bỏ nguy cơ SQL Injection.
    * Làm sạch dữ liệu đầu vào (`trim_whitespace`, giới hạn độ dài bằng `snprintf`) như yêu cầu trong mục 3.3 của `issue.md`.

* **Task 2.3: Quản lý Phiên Xác Thực & Chống Đăng Nhập Trùng (`REGISTER`, `LOGIN`, `LOGOUT`)**
  * *Công việc:*
    * Kiểm tra cờ `session->is_auth` trước mọi câu lệnh yêu cầu quyền (trả về `ERROR Please login first` nếu chưa đăng nhập).
    * Khi xử lý `LOGIN`, quét mảng `clients[]` đang online; nếu trùng username, từ chối ngay với `ERROR User '...' is already logged in on another session`.
    * Cập nhật trạng thái `last_seen` vào Database khi user `LOGOUT` hoặc ngắt kết nối.

---

### Giai Đoạn 3: Hiện Diện Thời Gian Thực, Chat 1-1, Chat Nhóm & Lịch Sử (Milestone 3)
> **Mục tiêu:** Hoàn thiện toàn bộ nghiệp vụ nhắn tin cốt lõi và lưu vết lịch sử trò chuyện.

* **Task 3.1: Hệ thống Quản lý Hiện diện Thời gian thực (Realtime Presence Push)**
  * *Công việc:*
    * Ngay khi User A đăng nhập thành công: Gửi `ONLINE_LIST <u1> <u2>...` cho A, đồng thời phát quảng bá `EVENT_ONLINE <A>` cho tất cả client khác.
    * Ngay khi User A đăng xuất hoặc rớt mạng: Phát quảng bá `EVENT_OFFLINE <A>` cho tất cả client còn lại.

* **Task 3.2: Định tuyến Nhắn tin 1-1 & Phiên Trò chuyện Trực tiếp (`/msg`, `/chat`, `/endchat`)**
  * *Công việc:*
    * Chặn tự nhắn cho chính mình (`ERROR Cannot send message to yourself`).
    * Hỗ trợ 2 chế độ khi người nhận không online: trả về lỗi `ERROR User '<target>' is not online` (theo chuẩn test cũ) hoặc lưu vào hàng đợi Offline Messages trong DB để tự động đẩy về khi người đó online lại.
    * Quản lý trạng thái `active_partner` khi dùng lệnh `/chat <username>` (`START_CHAT`) và `/endchat` (`END_CHAT`).
    * Khi một bên thoát phiên hoặc mất kết nối đột ngột, tự động gửi `NOTICE User '<username>' has disconnected.` cho đối tác đang chat để client kia thoát chế độ `/chat` an toàn.

* **Task 3.3: Quản lý Nhóm Chat & Đồng bộ CSDL (`JOIN`, `LEAVE`, `GROUP_MSG`, `GROUPS`, `MEMBERS`)**
  * *Công việc:*
    * Kiểm tra quyền thành viên trước khi gửi tin (`ERROR You must join group '<group>' first`).
    * Phát tán tin nhắn nhóm tới toàn bộ thành viên đang online (trừ người gửi) và lưu lịch sử vào bảng `messages`.
    * Khi một client ngắt kết nối, gọi `remove_client_from_all_groups()` đối với phiên runtime và phát sự kiện `GROUP_EVENT LEAVE <group> <username>` tới các thành viên còn lại.

* **Task 3.4: Tải Lịch sử Tin nhắn (Chat History Sync)**
  * *Công việc:* Bổ sung lệnh `/history <user/group> [limit]` hoặc tự động trả về 15 tin nhắn gần nhất từ SQLite khi người dùng gõ `/chat <user>` hoặc `/join <group>`.

---

### Giai Đoạn 4: Truyền Tải File & Hình Ảnh Qua C Socket (Milestone 4)
> **Mục tiêu:** Thiết kế cơ chế truyền đa phương tiện không chặn (Non-blocking Multiplexed File Transfer) giữa các Client.

* **Task 4.1: Thiết kế Giao thức Truyền Chunk & Quản lý Bộ đệm File (`src/server/file_transfer.c`)**
  * *Công việc:*
    * Định nghĩa các gói tin giao thức:
      * `FILE_INIT <target> <filename> <size> <sha256> <is_image>`
      * `FILE_ACCEPT <transfer_id>` / `FILE_REJECT <transfer_id>`
      * `FILE_CHUNK <transfer_id> <seq_num> <base64_data>`
      * `FILE_END <transfer_id>`
    * Cài đặt bộ mã hóa/giải mã Base64 tốc độ cao bằng C (hoặc framing nhị phân có độ dài) để dữ liệu nhị phân của ảnh/file không chứa ký tự `\n` làm hỏng bộ phân tích cú pháp lệnh.

* **Task 4.2: Cơ chế Gửi/Nhận File Đa Nhiệm Không Chặn ở Client (`src/client/net_handler.c`)**
  * *Công việc:*
    * Khi người dùng gõ `/sendfile <target> <path>` hoặc `/sendimg <target> <path>`, Client mở file (`fopen("rb")`), tính toán kích thước và mã băm SHA-256.
    * Thay vì dùng vòng lặp while gửi hết file ngay lập tức (gây đơ giao diện chat), Client đăng ký `file_job` vào vòng lặp `select()` (sử dụng tập `write_fds`). Mỗi khi socket sẵn sàng ghi, đẩy tiếp 1 chunk 4KB. Nhờ đó, người dùng vẫn có thể vừa gửi file 50MB vừa gõ chat văn bản bình thường.

* **Task 4.3: Kiểm tra Toàn vẹn File (SHA-256) & Xem Ảnh Trực Tiếp Trên Terminal**
  * *Công việc:*
    * Phía nhận ghép các chunk vào file tạm `.part` trong thư mục `storage/client_downloads/`. Khi nhận `FILE_END`, kiểm tra mã băm SHA-256; nếu trùng khớp mới đổi tên thành file chính thức.
    * Tích hợp tính năng render ảnh (PNG/JPG) sang chuỗi ký tự màu ANSI (sử dụng `stb_image.h`) để hiển thị bản xem trước (thumbnail) ngay trong khung trò chuyện khi nhận lệnh `/sendimg`.

---

### Giai Đoạn 5: Giao Diện Liên Tục (Continuous UI / TUI) (Milestone 5)
> **Mục tiêu:** Xây dựng giao diện dòng lệnh hiện đại, không bị trôi chữ khi đang gõ, cập nhật danh sách Online và tiến trình tải file theo thời gian thực.

* **Task 5.1: Thiết kế Bố cục Cửa sổ với `ncurses` / Raw Terminal (`src/client/ui_continuous.c`)**
  * *Công việc:*
    * Khởi tạo màn hình chia thành các vùng độc lập:
      * **Khung Trái (75% chiều rộng):** Cửa sổ lịch sử tin nhắn hỗ trợ cuộn (`scrollok`) và phân biệt màu sắc (Tin nhắn riêng, Tin nhắn nhóm, Thông báo hệ thống, Lỗi).
      * **Khung Phải (25% chiều rộng):** Danh sách Users Online và Joined Groups tự động vẽ lại mỗi khi nhận sự kiện `EVENT_ONLINE`, `EVENT_OFFLINE`, `ONLINE_LIST` từ server.
      * **Khung Dưới (2 dòng):** Dòng trạng thái (hiển thị chế độ `[Chatting with: bob]`, thanh tiến trình gửi file `[Uploading photo.png: 65%]`, báo hiệu đối phương đang gõ) và dòng nhập liệu (Input Box).

* **Task 5.2: Xử lý Nhập liệu Bất đồng bộ (Non-blocking Keystroke Handler)**
  * *Công việc:*
    * Đọc từng phím bấm từ `STDIN_FILENO` khi `select()` báo có sự kiện bàn phím: xử lý phím Backspace, phím mũi tên trái/phải, phím Tab (tự động hoàn thành tên lệnh `/msg`, `/sendfile` hoặc tên username đang online), phím PageUp/PageDown để cuộn lịch sử chat.
    * Đảm bảo khi tin nhắn mới hoặc chunk file đổ về từ socket, chỉ cửa sổ Chat hoặc Sidebar được vẽ lại (`wrefresh`), còn con trỏ chuột vẫn nằm yên tại vị trí đang gõ ở ô Input.

* **Task 5.3: Chế độ Tương thích CLI (`--headless` / `--simple-cli`)**
  * *Công việc:* Hỗ trợ cờ dòng lệnh `./client 127.0.0.1 8080 --raw` để tắt TUI ncurses và chạy ở chế độ đọc/ghi dòng chuẩn (`stdout`), giúp tương thích 100% với các script kiểm thử tự động bằng Python (`test_chatapp.py`).

---

### Giai Đoạn 6: Kiểm Thử Tự Động & Đóng Gói (Milestone 6)
> **Mục tiêu:** Đảm bảo độ ổn định tuyệt đối, không rò rỉ bộ nhớ (Memory Leak) và vượt qua mọi bài test chịu tải.

* **Task 6.1: Mở rộng Bộ Kiểm Thử Tự Động (`tests/test_chatapp_v2.py`)**
  * *Công việc:* Giữ nguyên 8 kịch bản kiểm thử gốc trong `README.md` và bổ sung thêm:
    1. Kiểm thử lưu trữ tài khoản bền vững qua SQLite3 sau khi khởi động lại server.
    2. Kiểm thử hiện tượng dính gói (Sticky Packet) bằng cách bắn 20 lệnh liên tiếp trong 1 buffer TCP duy nhất.
    3. Kiểm thử truyền file nhị phân và ảnh (so sánh mã băm MD5/SHA256 giữa file gốc và file nhận được).
    4. Kiểm thử nhận tin nhắn Offline và tải lại lịch sử chat.

* **Task 6.2: Kiểm tra Rò rỉ Bộ nhớ & Tài nguyên (Valgrind & Stress Test)**
  * *Công việc:*
    * Chạy server dưới sự giám sát của `valgrind --leak-check=full --track-fds=yes ./server 8080`.
    * Giả lập 100 clients kết nối, đăng nhập, chat nhóm, gửi file và ngắt kết nối đột ngột cùng lúc để xác nhận: không crash (`SIGPIPE`), không rò rỉ vùng nhớ `malloc`, và không rò rỉ File Descriptor.

---

## 4. Bảng Tổng Hợp Giao Thức Mở Rộng (Protocol Specification v2.0)

Bảng dưới đây mở rộng trực tiếp từ Mục 7 trong `issue.md`, bổ sung các lệnh xử lý Database, Lịch sử và Truyền File/Ảnh:

| Cú pháp Client gửi | Ý nghĩa | Phản hồi / Sự kiện từ Server |
| :--- | :--- | :--- |
| `REGISTER <user> <pass>` | Đăng ký tài khoản vào SQLite | `OK Registered successfully` hoặc `ERROR ...` |
| `LOGIN <user> <pass>` | Xác thực với Hash + Salt trong DB | `OK LOGIN_OK <user>` + `ONLINE_LIST ...` + `OFFLINE_MSGS ...` |
| `MSG <user> <nội dung>` | Gửi tin nhắn 1-1 (lưu vào DB) | `OK Sent to <user>` / `MSG_FROM <sender> <nội dung>` |
| `START_CHAT <user>` | Mở phiên chat 1-1 & tải lịch sử | `OK START_CHAT_OK <user>` + `HISTORY_ITEM ...` |
| `END_CHAT` | Kết thúc phiên chat 1-1 | `OK END_CHAT_OK` + báo `NOTICE` cho đối tác |
| `JOIN <nhóm>` / `LEAVE <nhóm>` | Tham gia / Rời nhóm (lưu vào DB) | `OK Joined/Left group` + `GROUP_EVENT JOIN/LEAVE ...` |
| `GROUP_MSG <nhóm> <msg>` | Gửi tin nhắn vào nhóm | `OK Group message delivered` / `GROUP_FROM <nhóm> <u1> <msg>` |
| `HISTORY <target> <limit>` | Lấy lịch sử chat 1-1 hoặc nhóm | `HISTORY_BEGIN` $\to$ `HISTORY_MSG ...` $\to$ `HISTORY_END` |
| `FILE_INIT <target> <name> <size> <sha256> <type>` | Khởi tạo truyền File hoặc Ảnh | `OK FILE_READY <transfer_id>` / `FILE_INCOMING <id> <from> <name> <size> <type>` |
| `FILE_CHUNK <id> <seq> <b64_data>` | Truyền 1 phân đoạn dữ liệu (4KB) | Chuyển tiếp `FILE_CHUNK` tới người nhận (Streaming) |
| `FILE_END <id>` | Kết thúc truyền file | `FILE_DONE <id> <sha256>` (Client tự đối chiếu Checksum) |
| `TYPING <target>` | Báo hiệu đang gõ phím | `EVENT_TYPING <sender>` |
| `PING` / `PONG` | Kiểm tra kết nối ngầm (Heartbeat) | `PONG` / Cập nhật `last_active` |

---

## 5. Lộ Trình Thực Hiện Đề Xuất (Timeline & Thứ Tự Ưu Tiên)

* **Tuần 1 (Core C Networking & Protocol Framing):** Hoàn thành Giai đoạn 1. Đảm bảo vòng lặp `select()`, bộ đệm tách dòng `\n` (`memmove`) và cơ chế dọn dẹp socket chạy ổn định 100%.
* **Tuần 2 (SQLite3 Auth & Core Chat Routing):** Hoàn thành Giai đoạn 2 & 3. Thay thế `users.txt` bằng SQLite3, băm mật khẩu SHA-256, hoàn thiện chat 1-1, chat nhóm, đẩy danh sách online và vượt qua toàn bộ 8 bài test của `test_chatapp.py`.
* **Tuần 3 (File/Image Streaming):** Hoàn thành Giai đoạn 4. Tích hợp truyền file/ảnh theo từng chunk qua `select()` mà không làm nghẽn luồng chat chữ, kèm kiểm tra checksum SHA-256.
* **Tuần 4 (Continuous UI & Stress Testing):** Hoàn thành Giai đoạn 5 & 6. Lắp ghép giao diện liên tục (`ncurses` hoặc ANSI Raw Mode), thêm hiển thị ảnh ASCII trên terminal và kiểm thử chịu tải với Valgrind.
