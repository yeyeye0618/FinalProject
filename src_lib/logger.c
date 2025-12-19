// src_lib/logger.c

#include "common.h"  // 引入 common.h 以取得 LogLevel 定義
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <pthread.h> // 使用 mutex 確保 thread-safe

// 全域變數 (只在此檔案內部看得到)
static FILE *log_file = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// 初始化 Logger
void init_logger(const char *filename) {
    if (filename) {
        log_file = fopen(filename, "a");
        if (!log_file) {
            perror("Failed to open log file, using stdout");
            log_file = stdout;
        }
    } else {
        log_file = stdout;
    }
}

// 寫入 Log 實作
void log_message(LogLevel level, const char *format, ...) {
    // 鎖定 Mutex，避免多執行緒同時寫入導致訊息錯亂
    pthread_mutex_lock(&log_mutex);

    // 1. 獲取當前時間
    time_t now;
    time(&now);
    struct tm *local = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", local);

    // 2. 設定層級字串
    const char *level_str = "INFO";
    if (level == LOG_ERROR) level_str = "ERROR";
    else if (level == LOG_DEBUG) level_str = "DEBUG";

    // 3. 輸出 Log 格式: [時間] [層級] 訊息
    fprintf(log_file, "[%s] [%s] ", time_str, level_str);

    // 4. 處理變動參數 (...)
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);

    fprintf(log_file, "\n");
    fflush(log_file); // 強制立即寫入，避免程式崩潰時 Log 沒寫進去

    pthread_mutex_unlock(&log_mutex);
}