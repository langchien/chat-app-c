#!/usr/bin/env bash
# ============================================================
# scripts/run_valgrind.sh
# Memory Leak & File Descriptor Leak Analysis using Valgrind
#
# Task 6.2: Valgrind Memcheck & Resource Leak Audit
# ============================================================

set -e

PORT=8099
VALGRIND_LOG="valgrind_report.log"

echo "============================================================"
echo "   CHẠY KIỂM TRA RÒ RỈ BỘ NHỚ & TÀI NGUYÊN (VALGRIND)"
echo "============================================================"

# Compile server and client with debug symbols
echo "[1/4] Biên dịch dự án với cờ -g -O0..."
make clean
make CFLAGS="-Wall -Wextra -g -O0 -I include"

# Check if valgrind is installed
if ! command -v valgrind &> /dev/null; then
    echo "[CẢNH BÁO] Không tìm thấy valgrind trên hệ thống."
    echo "Để cài đặt trên Ubuntu/Debian: sudo apt-get install valgrind"
    exit 1
fi

echo "[2/4] Khởi động Server dưới sự giám sát của Valgrind (Port $PORT)..."
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --track-fds=yes \
         --log-file="$VALGRIND_LOG" \
         ./server $PORT &

SERVER_PID=$!
sleep 2

echo "[3/4] Chạy các kịch bản kiểm thử client gửi/nhận dữ liệu..."
python3 tests/test_chatapp_v2.py || true

sleep 1

echo "[4/4] Gửi tín hiệu SIGINT dừng Server an toàn (Graceful Shutdown)..."
kill -2 $SERVER_PID
wait $SERVER_PID || true

echo ""
echo "============================================================"
echo "   BÁO CÁO KẾT QUẢ VALGRIND (Trích xuất từ $VALGRIND_LOG):"
echo "============================================================"
grep -E "definitely lost|indirectly lost|possibly lost|still reachable|ERROR SUMMARY|FILE DESCRIPTORS" "$VALGRIND_LOG" || cat "$VALGRIND_LOG"
echo "============================================================"
echo "Báo cáo chi tiết đã được lưu tại: $VALGRIND_LOG"
