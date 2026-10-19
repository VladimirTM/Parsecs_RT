#ifndef PARSECS_LOG_H
#define PARSECS_LOG_H

/* Comment out to compile LOG_SERIAL to an empty function. */
#define LOG_ENABLED

#define LOG_BUFFER_SIZE     256U
/* Longest wait for the previous USB CDC transfer before a message is dropped. */
#define LOG_TX_TIMEOUT_MS   10U

void LOG_SERIAL(const char *format, ...);

#endif
