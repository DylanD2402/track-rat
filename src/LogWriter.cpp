/**
 * Track Rat - Background log writer implementation
 *
 * The main loop pushes records into a LogRing (lock-free, one producer and
 * one consumer); the writer task takes them out and writes them.
 */

#include "LogWriter.h"
#include <atomic>
#include "Config.h"
#include "LogRing.h"
#include "esp_heap_caps.h"

namespace LogWriter {

enum : uint8_t { REC_OPEN = 1, REC_DATA = 2, REC_CLOSE = 3, REC_RENAME = 4 };
static const size_t BATCH_BYTES = 16384;  // one card write; bigger than any record
static const size_t MAX_RECORD = LogRing::MAX_RECORD;

static LogRing ring;
static std::atomic<uint32_t> written{0};
static std::atomic<uint32_t> slowest{0};
static fs::FS* fsys = nullptr;
static TaskHandle_t task = nullptr;

// Writer task only
static uint8_t* batch = nullptr;
static size_t batchLen = 0;
static File file;
static bool fileOk = false;
static char filePath[160];

// --- writer task -------------------------------------------------------------

static void writeBatch() {
  if (!batchLen) return;
  if (fileOk) {
    size_t n = file.write(batch, batchLen);
    written += n;
    if (n != batchLen) {
      DEBUG_PRINTF("Log: write to %s failed (card full or removed?) - rest of this session not logged\n", filePath);
      file.close();
      fileOk = false;
    }
  }
  batchLen = 0;
}

static void closeFile() {
  writeBatch();
  if (fileOk) {
    file.close();  // also syncs it
    DEBUG_PRINTF("Log: closed %s (%lu KB)\n", filePath, (unsigned long)(written / 1024));
  }
  fileOk = false;
}

static void drain() {
  uint8_t type;
  size_t len;
  if (!ring.next(type, len)) return;
  uint32_t start = millis();
  bool wrote = false;
  do {
    if (type == REC_OPEN) {
      closeFile();
      size_t n = len < sizeof(filePath) - 1 ? len : sizeof(filePath) - 1;
      ring.copyPayload(filePath, n);
      filePath[n] = 0;
      file = fsys->open(filePath, FILE_WRITE);
      fileOk = (bool)file;
      written = 0;
      if (!fileOk) DEBUG_PRINTF("Log: could not create %s\n", filePath);
    } else if (type == REC_DATA) {
      if (batchLen + len > BATCH_BYTES) writeBatch();
      ring.copyPayload(batch + batchLen);
      batchLen += len;
      wrote = true;
    } else if (type == REC_CLOSE) {
      closeFile();
      wrote = false;
    } else if (type == REC_RENAME) {
      // Payload "old\0new". FAT can't rename a file that's open, so write out
      // what's buffered, close, rename, and carry on appending to it.
      char paths[2 * sizeof(filePath)];
      size_t n = len < sizeof(paths) - 1 ? len : sizeof(paths) - 1;
      ring.copyPayload(paths, n);
      paths[n] = 0;
      const char* from = paths;
      const char* to = paths + strlen(paths) + 1;
      bool wasOpen = fileOk && strcmp(filePath, from) == 0;
      if (wasOpen) {
        writeBatch();
        file.close();
        fileOk = false;
      }
      bool ok = to < paths + n && fsys->rename(from, to);
      if (ok) DEBUG_PRINTF("Log: renamed %s to %s\n", from, to);
      else DEBUG_PRINTF("Log: could not rename %s to %s - still logging to the old name\n", from, to);
      if (wasOpen) {
        if (ok) strlcpy(filePath, to, sizeof(filePath));
        file = fsys->open(filePath, FILE_APPEND);
        fileOk = (bool)file;
        if (!fileOk) DEBUG_PRINTF("Log: could not reopen %s\n", filePath);
      }
    }
    ring.pop();
  } while (ring.next(type, len));
  writeBatch();
  if (wrote && fileOk) file.flush();  // fsync: the file's new length reaches the card
  uint32_t took = millis() - start;
  if (took > slowest) slowest = took;
}

static void writerTask(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LOG_FLUSH_MS));
    drain();
  }
}

// --- main loop side ----------------------------------------------------------

bool begin(fs::FS& fs) {
  if (task) return true;
  fsys = &fs;
  uint8_t* mem = nullptr;
  size_t size = LOG_BUFFER_BYTES;
  while (!mem && size >= 16384) {
    mem = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) size /= 2;
  }
  batch = (uint8_t*)heap_caps_malloc(BATCH_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!mem || !batch) {
    DEBUG_PRINTLN("Log: no memory for the log buffer - full-rate logging off");
    free(mem);
    free(batch);
    batch = nullptr;
    return false;
  }
  ring.attach(mem, size);
  // Core 0: the main loop (and lap timing) runs on core 1.
  if (xTaskCreatePinnedToCore(writerTask, "logwriter", 8192, nullptr, 1, &task, 0) != pdPASS) {
    DEBUG_PRINTLN("Log: could not start the writer task - full-rate logging off");
    task = nullptr;
    return false;
  }
  DEBUG_PRINTF("Log: %u KB buffer in PSRAM, written every %d ms from core 0\n", (unsigned)(size / 1024),
               LOG_FLUSH_MS);
  return true;
}

bool running() { return task != nullptr; }

static void retryClose();

bool open(const String& path, const char* header, size_t headerLen) {
  if (!task) return false;
  retryClose();
  const size_t records = 1 + (headerLen + MAX_RECORD - 1) / MAX_RECORD;
  if (path.length() > MAX_RECORD ||
      ring.freeSpace() < path.length() + headerLen + records * LogRing::RECORD_HEADER)
    return false;
  ring.push(REC_OPEN, path.c_str(), path.length());
  for (size_t off = 0; off < headerLen; off += MAX_RECORD) {
    size_t n = headerLen - off < MAX_RECORD ? headerLen - off : MAX_RECORD;
    ring.push(REC_DATA, header + off, n);
  }
  return true;
}

// A close that didn't fit in a full buffer: queued again as soon as there's room.
static bool closePending = false;
static void retryClose() {
  if (closePending && ring.push(REC_CLOSE, nullptr, 0)) {
    closePending = false;
    xTaskNotifyGive(task);
  }
}

bool append(const char* data, size_t len) {
  if (!task) return false;
  retryClose();
  return ring.push(REC_DATA, data, len);
}

bool rename(const String& from, const String& to) {
  if (!task) return false;
  retryClose();
  String payload = from;
  payload += '\0';
  payload += to;
  if (payload.length() > MAX_RECORD) return false;
  if (!ring.push(REC_RENAME, payload.c_str(), payload.length())) return false;
  xTaskNotifyGive(task);  // rename now rather than at the next flush
  return true;
}

void close() {
  if (!task) return;
  closePending = true;
  retryClose();
}

void poll() {
  if (task) retryClose();
}

uint32_t bytesWritten() { return written; }
uint32_t bufferedBytes() { return ring.used(); }
uint32_t slowestWriteMs() { return slowest; }

}  // namespace LogWriter
