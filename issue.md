# Tất Cả Các Trường Hợp Có Thể Gây Ra Vấn Đề Cần Xử Lý Cho Chat Application

Tài liệu này tổng hợp toàn diện các tình huống biên (edge cases), lỗi tiềm ẩn trong lập trình mạng socket TCP và các cơ chế xử lý tương ứng trong ứng dụng Chat Client - Server viết bằng ngôn ngữ C sử dụng mô hình I/O Multiplexing `select()`.

---

## 1. Vấn đề Tầng Giao Vận (Transport Layer - TCP)

### 1.1. Hiện tượng dính gói (Sticky Packet) và phân mảnh gói (Fragmentation)
- **Bản chất**: TCP là giao thức truyền theo luồng byte (byte-stream), không bảo toàn ranh giới thông điệp (message boundary). Nếu client gửi liên tiếp hai câu lệnh `LOGIN ...` và `MSG ...`, TCP có thể gộp cả hai vào một gói tin mạng (dính gói) hoặc tách một tin nhắn thành nhiều phần nhỏ (phân mảnh gói) do giới hạn MTU/MSS.
- **Vấn đề gây ra**: Nếu server chỉ dùng 1 lần `recv()` và coi toàn bộ dữ liệu nhận được là một tin nhắn, dữ liệu sẽ bị lỗi cú pháp hoặc mất mát.
- **Giải pháp xử lý**:
  - Phân định gói tin bằng ký tự xuống dòng `\n` (Delimiter-based framing).
  - Duy trì bộ đệm riêng `recv_buf` và con trỏ `recv_len` cho từng kết nối `ClientSession`.
  - Quét tìm ký tự `\n`, bóc tách từng thông điệp hoàn chỉnh để xử lý, sau đó dùng `memmove()` dồn các byte còn lại lên đầu bộ đệm.

### 1.2. Tín hiệu `SIGPIPE` và lỗi `EPIPE` (Broken Pipe)
- **Bản chất**: Khi một phía đã đóng socket (gửi FIN/RST) nhưng phía còn lại vẫn thực hiện `send()` hoặc `write()`, hệ điều hành Linux sẽ phát tín hiệu `SIGPIPE`. Mặc định hành vi của `SIGPIPE` là dừng chương trình (terminate process).
- **Vấn đề gây ra**: Một client ngắt kết nối đột ngột có thể làm **crash toàn bộ Chat Server**.
- **Giải pháp xử lý**:
  - Cài đặt bỏ qua tín hiệu này ngay đầu hàm `main()`: `signal(SIGPIPE, SIG_IGN);`.
  - Khi đó hàm `send()` sẽ trả về giá trị `-1` kèm mã lỗi `errno == EPIPE` thay vì làm sập server, cho phép đóng socket an toàn.

### 1.3. Lỗi chiếm dụng cổng khi khởi động lại (`EADDRINUSE`)
- **Bản chất**: Sau khi server tắt, socket vừa đóng sẽ rơi vào trạng thái `TIME_WAIT` (thường kéo dài từ 30 giây đến 2 phút để bảo đảm các gói tin trôi nổi trên mạng được giải phóng hết).
- **Vấn đề gây ra**: Khởi động lại server ngay lập tức sẽ thất bại tại lệnh `bind()` với lỗi `Address already in use`.
- **Giải pháp xử lý**:
  - Thiết lập cờ `SO_REUSEADDR` trước khi `bind()`:
    ```c
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ```

### 1.4. Mất kết nối âm thầm (Half-Open Connection)
- **Bản chất**: Client bị rút dây mạng, mất nguồn hoặc rớt mạng đột ngột mà không kịp gửi gói tin FIN. Server vẫn nghĩ client còn kết nối.
- **Giải pháp mở rộng**:
  - Bật tính năng TCP Keepalive (`SO_KEEPALIVE`) ở tầng socket hoặc cài đặt cơ chế Heartbeat (Ping/Pong) định kỳ ở tầng ứng dụng (application layer).

---

## 2. Vấn đề I/O Multiplexing & Quản Lý Socket

### 2.1. Giới hạn `FD_SETSIZE` trong `select()`
- **Bản chất**: Hàm `select()` sử dụng kiểu dữ liệu `fd_set`, bị giới hạn cứng bởi hằng số `FD_SETSIZE` (thường là 1024 trên Linux).
- **Vấn đề gây ra**: Nếu số lượng kết nối hoặc giá trị file descriptor vượt quá 1024, hành vi ghi vào `fd_set` sẽ bị tràn bộ nhớ (buffer overflow).
- **Giải pháp xử lý**:
  - Giới hạn số lượng client tối đa trong ứng dụng (`MAX_CLIENTS = 128`).
  - Khi vượt quá số lượng cho phép tại hàm `accept()`, gửi thông báo lỗi `ERROR Server is full` và đóng ngay `new_fd`.
  - Hướng nâng cấp cho quy mô lớn: Chuyển sang `poll()` hoặc `epoll()` trên Linux.

### 2.2. Lời gọi hệ thống bị ngắt bởi tín hiệu (`EINTR`)
- **Bản chất**: Trong lúc `select()` đang chờ sự kiện, nếu có một tín hiệu hệ thống (signal) được gửi tới tiến trình, `select()` sẽ trả về `-1` và gán `errno = EINTR`.
- **Giải pháp xử lý**:
  - Kiểm tra `if (errno == EINTR) continue;` để tiếp tục vòng lặp chờ I/O thay vì thoát ứng dụng.

### 2.3. Rò rỉ tài nguyên File Descriptor (FD Leak)
- **Bản chất**: Khi client ngắt kết nối (hàm `recv()` trả về 0 hoặc lỗi âm), nếu không gọi `close(sockfd)` và không đặt lại socket về `0`, số lượng file descriptor còn lại sẽ cạn kiệt theo thời gian.
- **Giải pháp xử lý**:
  - Gom toàn bộ logic dọn dẹp kết nối vào hàm `disconnect_client(index)`: đóng socket, xóa trạng thái xác thực, xóa khỏi nhóm, thông báo cho đối tác chat và gửi thông báo offline đến tất cả client khác.

---

## 3. Vấn đề Xác Thực (Authentication) & Quản Lý Phiên

### 3.1. Đăng nhập trùng lặp từ nhiều nơi (Duplicate Login)
- **Bản chất**: Một tài khoản (ví dụ `alice`) đã đăng nhập ở máy A, nhưng tiếp tục đăng nhập ở máy B.
- **Vấn đề gây ra**: Tin nhắn bị định tuyến sai, trạng thái session bị xung đột.
- **Giải pháp xử lý**:
  - Khi xử lý lệnh `LOGIN`, server duyệt qua toàn bộ session đang online: nếu đã có session nào mang username trùng khớp thì từ chối với thông báo `ERROR User '...' is already logged in on another session`.

### 3.2. Thực thi lệnh khi chưa xác thực (Unauthenticated Access)
- **Bản chất**: Client kết nối TCP thành công nhưng bỏ qua bước login mà gửi thẳng các lệnh như `MSG`, `JOIN`, `USERS`.
- **Giải pháp xử lý**:
  - Kiểm tra cờ `session->is_auth`. Trước khi đăng nhập thành công, chỉ cho phép các lệnh `REGISTER`, `LOGIN`, `QUIT`. Bất kỳ lệnh nào khác sẽ nhận thông báo `ERROR Please login first`.

### 3.3. Dữ liệu đầu vào độc hại hoặc không hợp lệ (Input Sanitization)
- **Bản chất**: Username/password chứa khoảng trắng, ký tự rỗng, hoặc chuỗi quá dài gây tràn bộ đệm tĩnh (`buffer overflow`).
- **Giải pháp xử lý**:
  - Định dạng đọc chuỗi giới hạn độ dài bằng `sscanf("%31s %63s")`.
  - Hàm `safe_copy` sử dụng `snprintf` bảo đảm luôn có ký tự kết thúc chuỗi `\0`.
  - Hàm `trim_whitespace` làm sạch các khoảng trắng dư thừa trước và sau câu lệnh.

---

## 4. Vấn đề Quản Lý Hiện Diện (Presence)

### 4.1. Đẩy danh sách online theo thời gian thực (Realtime Online Push)
- **Yêu cầu**: Khi có người đăng nhập hoặc ngắt kết nối, tất cả những người đang online phải nhận được cập nhật tức thì mà không cần phải chủ động gõ lệnh hỏi server.
- **Giải pháp xử lý**:
  - Khi User A đăng nhập: Server gửi `EVENT_ONLINE <username>` đến toàn bộ client đang online khác (trừ A). Đồng thời gửi danh sách `ONLINE_LIST <all_users>` đầy đủ về cho A.
  - Khi User A đăng xuất hoặc rớt mạng: Server tự động phát sự kiện `EVENT_OFFLINE <username>` tới toàn bộ các client còn lại.

---

## 5. Vấn đề Nhắn Tin 1-1 (Private Chat) & Phiên Trò Chuyện Trực Tiếp

### 5.1. Gửi tin nhắn cho người không tồn tại hoặc đã offline
- **Giải pháp xử lý**: Kiểm tra sự tồn tại trong danh sách đang online. Nếu không tìm thấy, gửi phản hồi lỗi `ERROR User '<target>' is not online` về cho người gửi.

### 5.2. Tự gửi tin nhắn cho chính mình
- **Giải pháp xử lý**: So sánh `target` với `session->username`, từ chối lệnh với thông báo `ERROR Cannot send message to yourself`.

### 5.3. Đối tác chat ngắt kết nối đột ngột trong phiên 1-1
- **Vấn đề**: Người dùng A và B đang mở phiên chat 1-1 (`/chat B`). B bị mất điện hoặc tắt máy đột ngột. Nếu A không biết, A sẽ tiếp tục nhắn tin vào hư vô.
- **Giải pháp xử lý**:
  - Server lưu trường `active_partner` trong phiên của mỗi client.
  - Khi một phía disconnect hoặc gửi lệnh `/endchat`, server sẽ tìm `active_partner` và gửi thông báo trực tiếp: `NOTICE User '<username>' has disconnected.` (hoặc `ended the 1-on-1 chat session`).
  - Phía Client khi nhận được thông báo này sẽ tự động xóa `active_partner` và đưa giao diện về trạng thái bình thường.

---

## 6. Vấn đề Nhắn Tin Nhóm (Group Chat)

### 6.1. Nhắn tin vào nhóm khi chưa tham gia nhóm
- **Giải pháp xử lý**: Kiểm tra người gửi có trong danh sách `grp->members` hay không. Nếu chưa, từ chối với thông báo `ERROR You must join group '<group>' first`.

### 6.2. Dọn dẹp thành viên khi ngắt kết nối
- **Vấn đề**: Client tắt chương trình nhưng vẫn lưu tên trong danh sách thành viên của các nhóm, dẫn đến việc chuyển tiếp tin nhắn bị lỗi.
- **Giải pháp xử lý**: Trong hàm `disconnect_client`, gọi `remove_client_from_all_groups(username)` và phát sự kiện `GROUP_EVENT LEAVE <group> <username>` tới các thành viên còn lại trong nhóm.

---

## 7. Bảng Tổng Hợp Mã Trạng Thái và Cú Pháp Giao Thức

| Cú pháp Client gửi | Ý nghĩa | Phản hồi từ Server |
| :--- | :--- | :--- |
| `REGISTER <user> <pass>` | Đăng ký tài khoản | `OK Registered successfully...` hoặc `ERROR ...` |
| `LOGIN <user> <pass>` | Đăng nhập | `OK LOGIN_OK <user>` + `ONLINE_LIST ...` |
| `LOGOUT` | Đăng xuất | `OK Logged out successfully.` |
| `USERS` | Lấy danh sách online | `ONLINE_LIST <u1> <u2> ...` |
| `MSG <user> <nội dung>` | Gửi tin nhắn 1-1 | `OK Sent to <user>` hoặc `ERROR ...` |
| `START_CHAT <user>` | Vào phiên chat 1-1 | `OK START_CHAT_OK <user>` |
| `END_CHAT` | Thoát phiên chat 1-1 | `OK END_CHAT_OK` |
| `JOIN <nhóm>` | Vào nhóm chat | `OK Joined group '<nhóm>'` |
| `LEAVE <nhóm>` | Rời nhóm chat | `OK Left group '<nhóm>'` |
| `GROUPS` | Xem các nhóm | `GROUPS_LIST <g1> <g2> ...` |
| `MEMBERS <nhóm>` | Xem thành viên nhóm | `MEMBERS_LIST <nhóm> <m1> ...` |
| `GROUP_MSG <nhóm> <msg>` | Gửi tin vào nhóm | `OK Group message delivered...` |
| `QUIT` | Ngắt kết nối | `OK Goodbye!` |