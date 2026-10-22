/* ============================================================
 * src/client/net_handler.c
 * Client-side network I/O and asynchronous file/image transfer.
 *
 * Task 4.2: Non-blocking Chunk-based File Sending & Receiving.
 * Task 4.3: SHA-256 Integrity Verification & Terminal Image Viewer.
 * Task 5.1/5.2: TUI-aware message routing and progress updates.
 * ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

#include "../../include/client_ui.h"
#include "../../include/protocol.h"
#include "../../include/crypto_utils.h"

/* Global file send and receive jobs */
FileSendJob g_send_job = {0};
FileRecvJob g_recv_job = {0};

/* Ensure client download directory exists */
static void ensure_download_dir(void) {
#if defined(_WIN32)
    mkdir("storage");
    mkdir("storage/client_downloads");
#else
    mkdir("storage", 0755);
    mkdir("storage/client_downloads", 0755);
#endif
}

/* Send a protocol command string terminated by \n to the server */
void send_to_server(const char *cmd) {
    if (g_sockfd < 0 || !cmd) return;
    char buf[BUFFER_SIZE + 64];
    snprintf(buf, sizeof(buf), "%s\n", cmd);
    send(g_sockfd, buf, strlen(buf), 0);
}

/* ============================================================
 * File Sending – Task 4.2
 * ============================================================ */
void start_file_send(const char *target, const char *filepath, bool is_image) {
    if (!target || !filepath) return;

    if (g_send_job.active) {
        ui_add_message_fmt("%s[LỖI] Đang có một tiến trình gửi file khác đang diễn ra. Vui lòng đợi hoàn tất.%s",
                           COLOR_RED, COLOR_RESET);
        return;
    }

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        ui_add_message_fmt("%s[LỖI] Không thể mở file '%s' để đọc. Vui lòng kiểm tra đường dẫn.%s",
                           COLOR_RED, filepath, COLOR_RESET);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0) {
        ui_add_message_fmt("%s[LỖI] File '%s' rỗng (0 bytes).%s", COLOR_RED, filepath, COLOR_RESET);
        fclose(fp);
        return;
    }

    if (size > MAX_FILE_SIZE) {
        ui_add_message_fmt("%s[LỖI] Dung lượng file (%ld bytes) vượt quá giới hạn cho phép (50MB).%s",
                           COLOR_RED, size, COLOR_RESET);
        fclose(fp);
        return;
    }

    char sha256_hex[SHA256_HEX_LEN + 1];
    if (sha256_file(filepath, sha256_hex) != 0) {
        ui_add_message_fmt("%s[LỖI] Không thể tính mã băm SHA-256 cho file '%s'.%s", COLOR_RED, filepath, COLOR_RESET);
        fclose(fp);
        return;
    }

    /* Extract filename without path */
    const char *basename = strrchr(filepath, '/');
    if (!basename) basename = strrchr(filepath, '\\');
    if (basename) basename++;
    else basename = filepath;

    memset(&g_send_job, 0, sizeof(g_send_job));
    g_send_job.fp = fp;
    g_send_job.filesize = size;
    g_send_job.bytes_sent = 0;
    g_send_job.seq_num = 0;
    g_send_job.chunk_size = MAX_CHUNK_SIZE;
    g_send_job.is_image = is_image;
    strncpy(g_send_job.target, target, sizeof(g_send_job.target) - 1);
    strncpy(g_send_job.filepath, filepath, sizeof(g_send_job.filepath) - 1);
    strncpy(g_send_job.filename, basename, sizeof(g_send_job.filename) - 1);
    strncpy(g_send_job.sha256, sha256_hex, sizeof(g_send_job.sha256) - 1);

    ui_add_message_fmt("%s[KHỞI TẠO] Đang gửi yêu cầu truyền %s '%s' (%ld bytes) tới '%s' (SHA-256: %.8s...)...%s",
                       COLOR_CYAN, is_image ? "ảnh" : "file", basename, size, target, sha256_hex, COLOR_RESET);

    /* Send FILE_INIT to server */
    char init_cmd[BUFFER_SIZE];
    snprintf(init_cmd, sizeof(init_cmd), "%s %s %s %ld %s %d",
             CMD_FILE_INIT, target, basename, size, sha256_hex, is_image ? 1 : 0);
    send_to_server(init_cmd);
}

void on_file_ready(const char *transfer_id, int chunk_size) {
    if (!transfer_id || strlen(transfer_id) == 0) return;
    strncpy(g_send_job.transfer_id, transfer_id, sizeof(g_send_job.transfer_id) - 1);
    if (chunk_size > 0 && chunk_size <= MAX_CHUNK_SIZE) {
        g_send_job.chunk_size = chunk_size;
    }
    g_send_job.active = true;

    ui_add_message_fmt("%s[TRUYỀN TẢI] Máy chủ đã sẵn sàng (Session: %s). Đang bắt đầu gửi các phân đoạn dữ liệu...%s",
                       COLOR_GREEN, transfer_id, COLOR_RESET);
    ui_update_status();
}

void send_next_file_chunk(void) {
    if (!g_send_job.active || !g_send_job.fp) return;

    unsigned char raw_buf[MAX_CHUNK_SIZE];
    size_t to_read = (size_t)g_send_job.chunk_size;
    if ((long)to_read > (g_send_job.filesize - g_send_job.bytes_sent)) {
        to_read = (size_t)(g_send_job.filesize - g_send_job.bytes_sent);
    }

    size_t bytes_read = fread(raw_buf, 1, to_read, g_send_job.fp);
    if (bytes_read > 0) {
        char b64_buf[MAX_BASE64_CHUNK_SIZE];
        base64_encode(raw_buf, bytes_read, b64_buf, sizeof(b64_buf));

        char chunk_cmd[BUFFER_SIZE];
        snprintf(chunk_cmd, sizeof(chunk_cmd), "%s %s %d %s",
                 CMD_FILE_CHUNK, g_send_job.transfer_id, g_send_job.seq_num, b64_buf);
        send_to_server(chunk_cmd);

        g_send_job.bytes_sent += (long)bytes_read;
        g_send_job.seq_num++;

        ui_update_status();
    }

    /* Finished sending all bytes */
    if (g_send_job.bytes_sent >= g_send_job.filesize || bytes_read == 0) {
        char end_cmd[128];
        snprintf(end_cmd, sizeof(end_cmd), "%s %s", CMD_FILE_END, g_send_job.transfer_id);
        send_to_server(end_cmd);

        fclose(g_send_job.fp);
        g_send_job.fp = NULL;
        g_send_job.active = false;

        ui_add_message_fmt("%s[HOÀN TẤT GỬI] Đã tải lên toàn bộ %ld bytes cho file '%s'. Đang đợi xác nhận từ máy chủ...%s",
                           COLOR_GREEN, g_send_job.filesize, g_send_job.filename, COLOR_RESET);
        ui_update_status();
    }
}

/* ============================================================
 * File Receiving – Task 4.2 & 4.3
 * ============================================================ */
void on_file_incoming(const char *transfer_id, const char *sender, const char *filename,
                      long filesize, const char *sha256, bool is_image) {
    ensure_download_dir();

    if (g_recv_job.active && g_recv_job.fp) {
        fclose(g_recv_job.fp);
        g_recv_job.fp = NULL;
    }

    memset(&g_recv_job, 0, sizeof(g_recv_job));
    strncpy(g_recv_job.transfer_id, transfer_id, sizeof(g_recv_job.transfer_id) - 1);
    strncpy(g_recv_job.sender, sender, sizeof(g_recv_job.sender) - 1);
    strncpy(g_recv_job.filename, filename, sizeof(g_recv_job.filename) - 1);
    strncpy(g_recv_job.expected_sha256, sha256 ? sha256 : "", sizeof(g_recv_job.expected_sha256) - 1);
    g_recv_job.expected_size = filesize;
    g_recv_job.received_bytes = 0;
    g_recv_job.seq_num = 0;
    g_recv_job.is_image = is_image;

    snprintf(g_recv_job.filepath_part, sizeof(g_recv_job.filepath_part),
             "storage/client_downloads/%s.part", filename);
    snprintf(g_recv_job.filepath_final, sizeof(g_recv_job.filepath_final),
             "storage/client_downloads/%s", filename);

    g_recv_job.fp = fopen(g_recv_job.filepath_part, "wb");
    if (!g_recv_job.fp) {
        ui_add_message_fmt("%s[LỖI] Không thể tạo file đệm '%s' để lưu dữ liệu tải về.%s",
                           COLOR_RED, g_recv_job.filepath_part, COLOR_RESET);
        return;
    }

    g_recv_job.active = true;

    ui_add_message_fmt("%s[FILE ĐẾN] Đang nhận %s '%s' (%ld bytes) từ '%s'...%s",
                       COLOR_MAGENTA, is_image ? "ẢNH" : "FILE", filename, filesize, sender, COLOR_RESET);
    ui_update_status();
}

void on_file_chunk(const char *transfer_id, int seq_num, const char *b64_data) {
    if (!g_recv_job.active || !g_recv_job.fp) return;
    if (strcmp(g_recv_job.transfer_id, transfer_id) != 0) return;

    (void)seq_num;
    unsigned char raw_buf[MAX_CHUNK_SIZE * 2];
    size_t decoded_len = base64_decode(b64_data, strlen(b64_data), raw_buf, sizeof(raw_buf));

    if (decoded_len > 0) {
        fwrite(raw_buf, 1, decoded_len, g_recv_job.fp);
        g_recv_job.received_bytes += (long)decoded_len;
        g_recv_job.seq_num++;
        ui_update_status();
    }
}

void on_file_done(const char *transfer_id, const char *sha256) {
    if (!g_recv_job.active || !g_recv_job.fp) return;
    if (strcmp(g_recv_job.transfer_id, transfer_id) != 0) return;

    fclose(g_recv_job.fp);
    g_recv_job.fp = NULL;

    /* Task 4.3: SHA-256 integrity check */
    char calc_sha256[SHA256_HEX_LEN + 1];
    if (sha256_file(g_recv_job.filepath_part, calc_sha256) != 0) {
        ui_add_message_fmt("%s[LỖI] Không thể tính mã băm SHA-256 file vừa tải về.%s", COLOR_RED, COLOR_RESET);
        remove(g_recv_job.filepath_part);
        g_recv_job.active = false;
        ui_update_status();
        return;
    }

    const char *expected = strlen(sha256) > 0 ? sha256 : g_recv_job.expected_sha256;
    if (strcasecmp(calc_sha256, expected) != 0) {
        ui_add_message_fmt("%s[CẢNH BÁO TOÀN VẸN] File '%s' bị sai lệch mã băm SHA-256! (Nhận: %.8s..., Kỳ vọng: %.8s...) - File đã bị loại bỏ.%s",
                           COLOR_RED, g_recv_job.filename, calc_sha256, expected, COLOR_RESET);
        remove(g_recv_job.filepath_part);
        g_recv_job.active = false;
        ui_update_status();
        return;
    }

    /* Checksum matches: rename .part to final destination */
    remove(g_recv_job.filepath_final);
    if (rename(g_recv_job.filepath_part, g_recv_job.filepath_final) != 0) {
        perror("rename");
    }

    ui_add_message_fmt("%s[TẢI VỀ THÀNH CÔNG] File '%s' (%ld bytes) đã lưu vào '%s' [SHA-256: %.8s...]%s",
                       COLOR_GREEN, g_recv_job.filename, g_recv_job.received_bytes,
                       g_recv_job.filepath_final, calc_sha256, COLOR_RESET);

    /* If it is an image, display preview on terminal */
    if (g_recv_job.is_image) {
        render_terminal_image(g_recv_job.filepath_final);
    }

    g_recv_job.active = false;
    ui_update_status();
}

/* ============================================================
 * Terminal Image Viewer – Task 4.3 & 5.1
 * Renders thumbnail in ANSI TrueColor / Half-blocks (▀)
 * ============================================================ */
void render_terminal_image(const char *filepath) {
    if (!filepath) return;

    FILE *fp = fopen(filepath, "rb");
    if (!fp) return;

    /* Check for BMP signature ('BM') */
    unsigned char header[54];
    if (fread(header, 1, 54, fp) == 54 && header[0] == 'B' && header[1] == 'M') {
        int width  = *(int*)&header[18];
        int height = *(int*)&header[22];
        int bpp    = *(short*)&header[28];
        int offset = *(int*)&header[10];

        if (width > 0 && height != 0 && (bpp == 24 || bpp == 32)) {
            bool top_down = (height < 0);
            if (height < 0) height = -height;

            ui_add_message_fmt("%s--- [XEM TRƯỚC ẢNH TERMINAL: %s (%dx%d %dbpp)] ---%s",
                               COLOR_CYAN, filepath, width, height, bpp, COLOR_RESET);

            /* Target terminal display size (scaled to fit nicely in chat window) */
            int target_w = 36;
            if (target_w > width) target_w = width;
            int target_h = (int)((long)height * target_w / width);
            if (target_h > 18) target_h = 18;
            if (target_h % 2 != 0) target_h++;

            /* Row stride in BMP is padded to 4 bytes */
            int row_stride = ((width * (bpp / 8) + 3) / 4) * 4;
            unsigned char *raw_data = malloc(row_stride * height);
            if (raw_data) {
                fseek(fp, offset, SEEK_SET);
                fread(raw_data, 1, row_stride * height, fp);

                /* Render using ANSI half-blocks (▀): 2 vertical pixels per line */
                char line_buf[2048];
                for (int y = 0; y < target_h; y += 2) {
                    int pos = 0;
                    for (int x = 0; x < target_w; x++) {
                        int src_x = (x * width) / target_w;
                        int src_y1 = top_down ? ((y * height) / target_h) : (height - 1 - ((y * height) / target_h));
                        int src_y2 = top_down ? (((y + 1) * height) / target_h) : (height - 1 - (((y + 1) * height) / target_h));

                        int idx1 = src_y1 * row_stride + src_x * (bpp / 8);
                        int idx2 = src_y2 * row_stride + src_x * (bpp / 8);

                        uint8_t b1 = raw_data[idx1 + 0], g1 = raw_data[idx1 + 1], r1 = raw_data[idx1 + 2];
                        uint8_t b2 = raw_data[idx2 + 0], g2 = raw_data[idx2 + 1], r2 = raw_data[idx2 + 2];

                        pos += snprintf(line_buf + pos, sizeof(line_buf) - pos,
                                        "\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm▀\033[0m",
                                        r1, g1, b1, r2, g2, b2);
                        if (pos >= (int)sizeof(line_buf) - 64) break;
                    }
                    ui_add_message(line_buf);
                }
                free(raw_data);
                ui_add_message_fmt("%s----------------------------------------------------%s", COLOR_CYAN, COLOR_RESET);
                fclose(fp);
                return;
            }
        }
    }
    fclose(fp);

    /* Fallback ASCII Banner for PNG/JPG/other image formats */
    ui_add_message_fmt("%s┌──────────────────────────────────────────────────┐%s", COLOR_CYAN, COLOR_RESET);
    ui_add_message_fmt("%s│            [ẢNH NHẬN ĐƯỢC: %-21.21s] │%s", COLOR_CYAN, filepath, COLOR_RESET);
    ui_add_message_fmt("%s│  🖼  Hình ảnh đã được lưu vào thư mục tải về:   │%s", COLOR_CYAN, COLOR_RESET);
    ui_add_message_fmt("%s│      %s%-40.40s%s│%s", COLOR_CYAN, COLOR_BOLD, filepath, COLOR_RESET, COLOR_CYAN, COLOR_RESET);
    ui_add_message_fmt("%s│  (Để xem ảnh kích thước gốc, mở trực tiếp file) │%s", COLOR_CYAN, COLOR_RESET);
    ui_add_message_fmt("%s└──────────────────────────────────────────────────┘%s", COLOR_CYAN, COLOR_RESET);
}
