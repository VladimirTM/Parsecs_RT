#ifndef PARSECS_LOG_H
#define PARSECS_LOG_H

/* Comment out to compile LOG_SERIAL to an empty function. */
#define LOG_ENABLED

/* Longest single formatted message. */
#define LOG_BUFFER_SIZE     256U
/* RAM queue between LOG_SERIAL and the log task. Messages that do not fit are dropped. */
#define LOG_RING_SIZE       8192U
/* Most bytes handed to one CDC_Transmit_FS call. */
#define LOG_CHUNK_SIZE      512U

void LOG_SERIAL(const char *format, ...);
void LOG_TaskStart(void *argument);

#endif
