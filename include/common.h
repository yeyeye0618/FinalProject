// include/common.h

#ifndef COMMON_H  // 防止重複 include 的保護機制
#define COMMON_H

#include <stdint.h> // 用於 uint32_t, uint16_t 等固定長度型別
#include <stddef.h>
// ==========================================
// 1. 操作碼定義 (OpCodes)
// ==========================================
// 用於告訴 Server 這次請求是要做什麼
#define OP_LOGIN              0x0000 // 登入請求 (取得 Session ID)
#define OP_QUERY_AVAILABILITY 0x0001 // 查詢剩餘票數
#define OP_BOOK_TICKET        0x0002 // 訂票請求
#define OP_RESPONSE_SUCCESS   0x1001 // 操作成功
#define OP_RESPONSE_FAIL      0x1002 // 操作失敗

#define XOR_KEY 0x42 // 簡單 XOR 金鑰

// ==========================================
// 2. 協定標頭 (Header) - 固定 16 bytes (原 8 bytes)
// ==========================================
// __attribute__((packed)) 告訴編譯器不要進行記憶體對齊 (Padding)
typedef struct __attribute__((packed)) {
    uint32_t packet_len;  // 封包總長度 (Header + Body 的 bytes 數)
    uint16_t opcode;      // 操作類型
    uint16_t req_id;      // 請求 ID
    uint32_t checksum;    // 封包檢查碼 (Header + Body)
    uint32_t session_id;  // Session ID (0 表示未登入/Login 請求)
} ProtocolHeader;

// ==========================================
// 3. 資料內容 (Payload/Body) 結構
// ==========================================

// 訂票請求的 Body (當 OpCode = OP_BOOK_TICKET)
typedef struct __attribute__((packed)) {
    uint32_t num_tickets; // 想買幾張票
    uint32_t user_id;     // 使用者 ID (模擬用)
} BookRequest;

// 伺服器回應的 Body (所有 Response 通用)
typedef struct __attribute__((packed)) {
    uint32_t remaining_tickets; // 剩餘票數
    char message[64];           // 伺服器回傳的訊息 (如 "Booking Success")
} ServerResponse;

// ==========================================
// 4. Logger 定義
// ==========================================
typedef enum {
    LOG_INFO,
    LOG_ERROR,
    LOG_DEBUG
} LogLevel;

// 初始化 Logger (可選擇輸出到檔案或 stdout)
void init_logger(const char *filename);

// 寫入 Log
void log_message(LogLevel level, const char *format, ...);


// ==========================================
// 5. 函數原型宣告 (Prototypes)
// ==========================================
// 這些函數實作在 src_lib/protocol.c 中

// 計算 Checksum (簡單加總或 CRC)
uint32_t calculate_checksum(const void *data, size_t len);

// XOR 加解密 (In-place)
void xor_cipher(void *data, size_t len);

// 基礎網路讀寫 (處理 TCP 黏包/斷包問題)
int read_n_bytes(int sockfd, void *buffer, int n);
int write_n_bytes(int sockfd, void *buffer, int n);


// ==========================================
// 6. 網路交互原型宣告 (Prototypes)
// ==========================================
// 這些函數實作在 src_lib/network.c 中

// 建立 Server Socket (socket -> bind -> listen)
// 回傳: sockfd 或 -1 (失敗)
int create_server_socket(int port);

// 建立 Client Socket 並連線 (socket -> connect)
// 回傳: sockfd 或 -1 (失敗)
int connect_to_server(const char *ip, int port);


#endif // COMMON_H