/* ============================================================
 * src/client/net_handler.c
 * Client-side network I/O and asynchronous file/image transfer.
 *
 * Task 4.2: Non-blocking Chunk-based File Sending & Receiving.
 * Task 4.3: SHA-256 Integrity Verification & Terminal Image Viewer.
 * ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
        printf("%s[LỖI] Đang có một tiến trình gửi file khác đang diễn ra. Vui lòng đợi hoàn tất.%s\n",
               COLOR_RED, COLOR_RESET);
        print_prompt();
        return;
    }

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        printf("%s[LỖI] Không thể mở file '%s' để đọc. Vui lòng kiểm tra đường dẫn.%s\n",
               COLOR_RED, filepath, COLOR_RESET);
        print_prompt();
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0) {
        printf("%s[LỖI] File '%s' rỗng (0 bytes).%s\n", COLOR_RED, filepath, COLOR_RESET);
        fclose(fp);
        print_prompt();
        return;
    }

    if (size > MAX_FILE_SIZE) {
        printf("%s[LỖI] Dung lượng file (%ld bytes) vượt quá giới hạn cho phép (50MB).%s\n",
               COLOR_RED, size, COLOR_RESET);
        fclose(fp);
        print_prompt();
        return;
    }

    char sha256_hex[SHA256_HEX_LEN + 1];
    if (sha256_file(filepath, sha256_hex) != 0) {
        printf("%s[LỖI] Không thể tính mã băm SHA-256 cho file '%s'.%s\n", COLOR_RED, filepath, COLOR_RESET);
        fclose(fp);
        print_prompt();
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

    printf("%s[KHỞI TẠO] Đang gửi yêu cầu truyền %s '%s' (%ld bytes) tới '%s' (SHA-256: %.8s...)...%s\n",
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

    printf("\r\033[K%s[TRUYỀN TẢI] Máy chủ đã sẵn sàng (Session: %s). Đang bắt đầu gửi các phân đoạn dữ liệu...%s\n",
           COLOR_GREEN, transfer_id, COLOR_RESET);
    print_prompt();
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

        int pct = (int)((g_send_job.bytes_sent * 100) / g_send_job.filesize);
        if (pct % 25 == 0 || g_send_job.bytes_sent >= g_send_job.filesize) {
            printf("\r\033[K%s[TIẾN ĐỘ TẢI LÊN] Đang gửi '%s': [%d%%] (%ld/%ld bytes)%s",
                   COLOR_CYAN, g_send_job.filename, pct, g_send_job.bytes_sent, g_send_job.filesize, COLOR_RESET);
            fflush(stdout);
        }
    }

    /* Finished sending all bytes */
    if (g_send_job.bytes_sent >= g_send_job.filesize || bytes_read == 0) {
        char end_cmd[128];
        snprintf(end_cmd, sizeof(end_cmd), "%s %s", CMD_FILE_END, g_send_job.transfer_id);
        send_to_server(end_cmd);

        fclose(g_send_job.fp);
        g_send_job.fp = NULL;
        g_send_job.active = false;

        printf("\n%s[HOÀN TẤT GỬI] Đã tải lên toàn bộ %ld bytes cho file '%s'. Đang đợi xác nhận từ máy chủ...%s\n",
               COLOR_GREEN, g_send_job.filesize, g_send_job.filename, COLOR_RESET);
        print_prompt();
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
        printf("\r\033[K%s[LỖI] Không thể tạo file đệm '%s' để lưu dữ liệu tải về.%s\n",
               COLOR_RED, g_recv_job.filepath_part, COLOR_RESET);
        print_prompt();
        return;
    }

    g_recv_job.active = true;

    printf("\r\033[K%s[FILE ĐẾN] Đang nhận %s '%s' (%ld bytes) từ '%s'...%s\n",
           COLOR_MAGENTA, is_image ? "ẢNH" : "FILE", filename, filesize, sender, COLOR_RESET);
    print_prompt();
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
        printf("\r\033[K%s[LỖI] Không thể tính mã băm SHA-256 file vừa tải về.%s\n", COLOR_RED, COLOR_RESET);
        remove(g_recv_job.filepath_part);
        g_recv_job.active = false;
        print_prompt();
        return;
    }

    const char *expected = strlen(sha256) > 0 ? sha256 : g_recv_job.expected_sha256;
    if (strcasecmp(calc_sha256, expected) != 0) {
        printf("\r\033[K%s[CẢNH BÁO TOÀN VẸN] File '%s' bị sai lệch mã băm SHA-256! "
               "(Nhận: %.8s..., Kỳ vọng: %.8s...) - File đã bị loại bỏ.%s\n",
               COLOR_RED, g_recv_job.filename, calc_sha256, expected, COLOR_RESET);
        remove(g_recv_job.filepath_part);
        g_recv_job.active = false;
        print_prompt();
        return;
    }

    /* Checksum matches: rename .part to final destination */
    remove(g_recv_job.filepath_final);
    if (rename(g_recv_job.filepath_part, g_recv_job.filepath_final) != 0) {
        perror("rename");
    }

    printf("\r\033[K%s[TẢI VỀ THÀNH CÔNG] File '%s' (%ld bytes) đã lưu vào '%s' [SHA-256 verified: %.8s...]%s\n",
           COLOR_GREEN, g_recv_job.filename, g_recv_job.received_bytes,
           g_recv_job.filepath_final, calc_sha256, COLOR_RESET);

    /* If it is an image, display preview on terminal */
    if (g_recv_job.is_image) {
        render_terminal_image(g_recv_job.filepath_final);
    }

    g_recv_job.active = false;
    print_prompt();
}

/* ============================================================
 * Terminal Image Viewer – Task 4.3
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

            printf("\n%s--- [XEM TRƯỚC ẢNH TERMINAL: %s (%dx%d %dbpp)] ---%s\n",
                   COLOR_CYAN, filepath, width, height, bpp, COLOR_RESET);

            /* Target terminal display size */
            int target_w = 40;
            if (target_w > width) target_w = width;
            int target_h = (int)((long)height * target_w / width);
            if (target_h > 24) target_h = 24;
            if (target_h % 2 != 0) target_h++;

            /* Row stride in BMP is padded to 4 bytes */
            int row_stride = ((width * (bpp / 8) + 3) / 4) * 4;
            unsigned char *raw_data = malloc(row_stride * height);
            if (raw_data) {
                fseek(fp, offset, SEEK_SET);
                fread(raw_data, 1, row_stride * height, fp);

                /* Render using ANSI half-blocks (▀): 2 vertical pixels per line */
                for (int y = 0; y < target_h; y += 2) {
                    for (int x = 0; x < target_w; x++) {
                        int src_x = (x * width) / target_w;
                        int src_y1 = top_down ? ((y * height) / target_h) : (height - 1 - ((y * height) / target_h));
                        int src_y2 = top_down ? (((y + 1) * height) / target_h) : (height - 1 - (((y + 1) * height) / target_h));

                        int idx1 = src_y1 * row_stride + src_x * (bpp / 8);
                        int idx2 = src_y2 * row_stride + src_x * (bpp / 8);

                        uint8_t b1 = raw_data[idx1 + 0], g1 = raw_data[idx1 + 1], r1 = raw_data[idx1 + 2];
                        uint8_t b2 = raw_data[idx2 + 0], g2 = raw_data[idx2 + 1], r2 = raw_data[idx2 + 2];

                        /* ANSI 24-bit TrueColor: foreground top, background bottom */
                        printf("\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm▀\033[0m",
                               r1, g1, b1, r2, g2, b2);
                    }
                    printf("\n");
                }
                free(raw_data);
                printf("%s----------------------------------------------------%s\n\n", COLOR_CYAN, COLOR_RESET);
                fclose(fp);
                return;
            }
        }
    }
    fclose(fp);

    /* Fallback ASCII Banner for PNG/JPG/other image formats */
    printf("\n%s┌──────────────────────────────────────────────────┐%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s│            [ẢNH NHẬN ĐƯỢC: %-21.21s] │%s\n", COLOR_CYAN, filepath, COLOR_RESET);
    printf("%s│  🖼  Hình ảnh đã được lưu vào thư mục tải về:   │%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s│      %s%-40.40s%s│%s\n", COLOR_CYAN, COLOR_BOLD, filepath, COLOR_RESET, COLOR_CYAN, COLOR_RESET);
    printf("%s│  (Để xem ảnh kích thước gốc, mở trực tiếp file) │%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s└──────────────────────────────────────────────────┘%s\n\n", COLOR_CYAN, COLOR_RESET);
}
