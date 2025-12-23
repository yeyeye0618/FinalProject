// src_lib/protocol.c

#include "common.h"  // 引入我們定義的結構
#include <unistd.h>  // 用於 read, write
#include <stdio.h>   // 用於 perror
#include <errno.h>   // 用於 errno

// ==========================================
// 函數: calculate_checksum
// 功能: 計算資料的簡易 Checksum (加總)
// ==========================================
uint32_t calculate_checksum(const void *data, size_t len) {
    const uint8_t *ptr = (const uint8_t *)data;
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += ptr[i];
    }
    return sum;
}

// ==========================================
// 函數: xor_cipher
// 功能: 對資料進行 XOR 加密/解密
// ==========================================
void xor_cipher(void *data, size_t len) {
    uint8_t *ptr = (uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        ptr[i] ^= XOR_KEY;
    }
}

// ==========================================
// 函數: read_n_bytes
// 功能: 從 socket 讀取 "確切" n 個 bytes
// 原因: read() 可能只讀到一半資料就返回，必須用迴圈讀滿 n bytes
// ==========================================
int read_n_bytes(int sockfd, void *buffer, int n) {
    int total_read = 0;              // 目前已經讀到的 bytes 數
    int bytes_left = n;              // 還剩下多少 bytes 沒讀
    char *ptr = (char *)buffer;      // 指標用來移動寫入位置
    int ret;                         // 每次 read 的回傳值

    while (total_read < n) {
        // 嘗試讀取剩下的 bytes
        ret = read(sockfd, ptr + total_read, bytes_left);

        if (ret < 0) {
            if (errno == EINTR) {    // 訊號中斷
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(stderr, "Request Timed Out (read)\n");
                return -1;
            }
            perror("read_n_bytes error");
            return -1;
        } else if (ret == 0) {       // 關閉連線
            return 0;
        }

        total_read += ret;
        bytes_left -= ret;
    }

    return total_read;
}

// ==========================================
// 函數: write_n_bytes
// 功能: 寫入 "確切" n 個 bytes 到 socket
// 原因: write() 也可能只寫入一部分，必須用迴圈確保全部寫出
// ==========================================
int write_n_bytes(int sockfd, void *buffer, int n) {
    int total_written = 0;           // 目前已經寫入的 bytes 數
    int bytes_left = n;              // 還剩下多少 bytes 沒寫
    const char *ptr = (const char *)buffer; // 指標用來移動讀取位置
    int ret;                         // 每次 write 的回傳值

    while (total_written < n) {
        // 嘗試寫入剩下的 bytes
        ret = write(sockfd, ptr + total_written, bytes_left);

        if (ret <= 0) {
            if (ret < 0 && errno == EINTR) { // 訊號中斷
                continue;
            }
            if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                fprintf(stderr, "Request Timed Out (write)\n");
                return -1;
            }
            perror("write_n_bytes error");
            return -1;
        }

        total_written += ret;
        bytes_left -= ret;
    }

    return total_written;
}