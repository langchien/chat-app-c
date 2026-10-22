# Danh Sách Tasks Triển Khai Chat C

> **Tiến độ tổng quan:** 19/19 tasks hoàn thành (100%) - HOÀN THÀNH TOÀN BỘ DỰ ÁN

---

## Giai Đoạn 1: Nền Tảng Mạng C & Kiến Trúc Cốt Lõi (Milestone 1)
- [x] Task 1.1: Thiết lập Cấu trúc Dự án & Hệ thống Build (`Makefile`)
- [x] Task 1.2: Cài đặt Core TCP Server & Quản lý Socket (`src/server/main.c`, `session.c`)
- [x] Task 1.3: Bộ Phân Tách Gói Tin & Chống Sticky Packet (`src/common/protocol.c`)
- [x] Task 1.4: Cơ chế Heartbeat & Bắt Tín Hiệu Hệ Thống (Keep-Alive & Graceful Shutdown)

## Giai Đoạn 2: Xác Thực Cơ Sở Dữ Liệu & Bảo Mật (Milestone 2)
- [x] Task 2.1: Thiết kế Schema & Tích hợp SQLite3 C API (`src/server/database.c`)
- [x] Task 2.2: Băm Mật Khẩu & Chống SQL Injection (`src/common/crypto_utils.c`)
- [x] Task 2.3: Quản lý Phiên Xác Thực & Chống Đăng Nhập Trùng (`REGISTER`, `LOGIN`, `LOGOUT`)

## Giai Đoạn 3: Hiện Diện Thời Gian Thực, Chat 1-1, Chat Nhóm & Lịch Sử (Milestone 3)
- [x] Task 3.1: Hệ thống Quản lý Hiện diện Thời gian thực (Realtime Presence Push)
- [x] Task 3.2: Định tuyến Nhắn tin 1-1 & Phiên Trò chuyện Trực tiếp (`/msg`, `/chat`, `/endchat`)
- [x] Task 3.3: Quản lý Nhóm Chat & Đồng bộ CSDL (`JOIN`, `LEAVE`, `GROUP_MSG`, `GROUPS`, `MEMBERS`)
- [x] Task 3.4: Tải Lịch sử Tin nhắn (Chat History Sync)

## Giai Đoạn 4: Truyền Tải File & Hình Ảnh Qua C Socket (Milestone 4)
- [x] Task 4.1: Thiết kế Giao thức Truyền Chunk & Quản lý Bộ đệm File (`src/server/file_transfer.c`)
- [x] Task 4.2: Cơ chế Gửi/Nhận File Đa Nhiệm Không Chặn ở Client (`src/client/net_handler.c`)
- [x] Task 4.3: Kiểm tra Toàn vẹn File (SHA-256) & Xem Ảnh Trực Tiếp Trên Terminal

## Giai Đoạn 5: Giao Diện Liên Tục (Continuous UI / TUI) (Milestone 5)
- [x] Task 5.1: Thiết kế Bố cục Cửa sổ với Pure ANSI / Raw Terminal (`src/client/ui_continuous.c`)
- [x] Task 5.2: Xử lý Nhập liệu Bất đồng bộ (Non-blocking Keystroke Handler)
- [x] Task 5.3: Chế độ Tương thích CLI (`--headless` / `--simple-cli`)

## Giai Đoạn 6: Kiểm Thử Tự Động & Đóng Gói (Milestone 6)
- [x] Task 6.1: Mở rộng Bộ Kiểm Thử Tự Động (`tests/test_chatapp_v2.py`)
- [x] Task 6.2: Kiểm tra Rò rỉ Bộ nhớ & Tài nguyên (Valgrind & Stress Test)
