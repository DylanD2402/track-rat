/**
 * Track Rat - Background log writer
 *
 * Appends the full-rate session log to the SD card without ever holding up
 * the main loop. Lines go into a buffer in PSRAM; a task on core 0 (the main
 * loop runs on core 1) writes them out and flushes the file about once a
 * second, so a slow card write never delays lap timing, and a power cut
 * loses at most the last second.
 *
 * One file at a time: open(), append() lines, close().
 */

#ifndef LOG_WRITER_H
#define LOG_WRITER_H

#include <Arduino.h>
#include <FS.h>

namespace LogWriter {

// Allocates the buffer and starts the writer task. False if out of memory.
bool begin(fs::FS& fs);
bool running();

// These only queue the work; the writer task does it. False if the buffer
// is full.
bool open(const String& path, const char* header, size_t headerLen);
bool append(const char* data, size_t len);
void close();  // also writes out what's buffered straight away
// Renames a file in order with the queued writes. If it's the one being
// written, it's closed first and logging carries on under the new name.
// The rename itself happens on the writer task; false if it couldn't be queued.
bool rename(const String& from, const String& to);
void poll();   // call from the main loop: finishes a close() the full buffer held up

// Statistics, for the log
uint32_t bytesWritten();      // to the current file
uint32_t bufferedBytes();     // waiting to be written
uint32_t slowestWriteMs();    // longest single write + flush so far

}  // namespace LogWriter

#endif  // LOG_WRITER_H
