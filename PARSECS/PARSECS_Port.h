#ifndef PARSECS_PORT_H
#define PARSECS_PORT_H

#include "stm32f4xx.h"
#include <ringbuf.h>
#include <stdbool.h>
#include <stdint.h>

static inline uint32_t PARSECS_IrqLock(void)
{
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	return primask;
}

static inline void PARSECS_IrqUnlock(uint32_t primask)
{
	__set_PRIMASK(primask);
}

static inline bool PARSECS_RingFull(tRingBufObject *ring)
{
	uint32_t key = PARSECS_IrqLock();
	bool full = (RingBufFull(ring) != 0);
	PARSECS_IrqUnlock(key);
	return full;
}

static inline bool PARSECS_RingEmpty(tRingBufObject *ring)
{
	uint32_t key = PARSECS_IrqLock();
	bool empty = (RingBufEmpty(ring) != 0);
	PARSECS_IrqUnlock(key);
	return empty;
}

static inline uint8_t PARSECS_RingReadOne(tRingBufObject *ring)
{
	uint32_t key = PARSECS_IrqLock();
	uint8_t data = RingBufReadOne(ring);
	PARSECS_IrqUnlock(key);
	return data;
}

static inline void PARSECS_RingWriteOne(tRingBufObject *ring, uint8_t data)
{
	uint32_t key = PARSECS_IrqLock();
	RingBufWriteOne(ring, data);
	PARSECS_IrqUnlock(key);
}

static inline uint8_t PARSECS_RingPeekOne(const tRingBufObject *ring)
{
	uint32_t key = PARSECS_IrqLock();
	uint8_t data = ring->pucBuf[ring->ulReadIndex];
	PARSECS_IrqUnlock(key);
	return data;
}

static inline void PARSECS_RingAdvanceRead(tRingBufObject *ring, unsigned long count)
{
	uint32_t key = PARSECS_IrqLock();
	RingBufAdvanceRead(ring, count);
	PARSECS_IrqUnlock(key);
}

#endif
