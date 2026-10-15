/*
 * Host test for the Layer 3 stop-and-wait ARQ.
 *
 * Two PARSECS nodes (slaves[A] and slaves[B] of a master build) are wired
 * back to back by a Layer 1 stand-in that moves one byte each way per round,
 * like the I2C pump, and can drop or flip bytes. Layer 2 and Layer 3 are the
 * real firmware sources.
 */

#include <stdio.h>
#include <string.h>

#include "PARSECS_LowLevelAPI.h"
#include "PARSECS_Layer2.h"
#include "PARSECS_Layer3.h"

#define FRAME_OVERHEAD      DATA_ENCODING_SIZE
#define MAX_TEST_TICKS      20000

typedef struct
{
	int drop;           /* real bytes still to drop */
	int corrupt_at;     /* flip the Nth real byte from now (1-based), 0 = off */
} wire_t;

static wire_t a_to_b;
static wire_t b_to_a;
static bool peer_absent;
static int8_t A;
static int8_t B;
static int failures;
static int checks;

#define CHECK(cond) \
	do { \
		checks++; \
		if (!(cond)) { \
			printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++; \
		} \
	} while (0)

static void move_byte(SLAVE *from, SLAVE *to, wire_t *wire)
{
	uint8_t byte = 0x00U;
	bool real = !RingBufEmpty(&from->l1_buffer_tx);

	if (real)
	{
		byte = RingBufReadOne(&from->l1_buffer_tx);
		if (wire->drop > 0)
		{
			wire->drop--;
			return;
		}
		if ((wire->corrupt_at > 0) && (--wire->corrupt_at == 0))
		{
			byte ^= 0xFFU;
		}
	}
	if (!RingBufFull(&to->l1_buffer_rx))
	{
		RingBufWriteOne(&to->l1_buffer_rx, byte);
	}
}

/* Same job order as PARSECS_LowLevelTask, one Layer 1 round per tick. */
static void tick(void)
{
	SLAVE *a = &slaves[A];
	SLAVE *b = &slaves[B];

	PARSECS_TRANSMIT_LAYER3(a);
	PARSECS_TRANSMIT_LAYER2(a);
	PARSECS_TRANSMIT_LAYER3(b);
	PARSECS_TRANSMIT_LAYER2(b);

	/* Master side counts attempted rounds, so an absent slave still times out. */
	a->l1_rounds++;
	if (peer_absent)
	{
		if (!RingBufEmpty(&a->l1_buffer_tx))
		{
			(void)RingBufReadOne(&a->l1_buffer_tx);
		}
	}
	else
	{
		move_byte(a, b, &a_to_b);
		move_byte(b, a, &b_to_a);
		b->l1_rounds++;
	}

	PARSECS_RECEIVE_LAYER2(a);
	PARSECS_RECEIVE_LAYER3(a);
	PARSECS_RECEIVE_LAYER2(b);
	PARSECS_RECEIVE_LAYER3(b);
}

static void setup(void)
{
	PARSECS_Data_Init();
	A = PARSECS_Add_Slave(NULL, NULL);
	B = PARSECS_Add_Slave(NULL, NULL);
	memset(&a_to_b, 0, sizeof(a_to_b));
	memset(&b_to_a, 0, sizeof(b_to_a));
	peer_absent = false;
}

/* Ticks until `node` hands a DATA frame to the High Level, or -1. */
static int run_until_rx(int8_t node, uint8_t *data, uint8_t *len, uint8_t *seq)
{
	int t;
	for (t = 1; t <= MAX_TEST_TICKS; t++)
	{
		tick();
		if (PARSECS_RECEIVE_APP((uint8_t)node, data, len, seq) == 0)
		{
			return t;
		}
	}
	return -1;
}

/* Ticks until `node` accepts a new DATA frame again, or -1. */
static int run_until_tx_ready(int8_t node)
{
	int t;
	for (t = 1; t <= MAX_TEST_TICKS; t++)
	{
		tick();
		if (PARSECS_CHECK_APP_TX_READY((uint8_t)node) == 0)
		{
			return t;
		}
	}
	return -1;
}

static uint32_t retx_of(int8_t node)
{
	uint32_t retx = 0U;
	(void)PARSECS_Get_ARQ_Stats((uint8_t)node, &retx, NULL);
	return retx;
}

static uint32_t dropped_of(int8_t node)
{
	uint32_t dropped = 0U;
	(void)PARSECS_Get_ARQ_Stats((uint8_t)node, NULL, &dropped);
	return dropped;
}

static void inject_frame(SLAVE *to, uint8_t type, uint8_t seq)
{
	CRC16 crc;
	uint16_t value;

	CRC16_constructor(&crc);
	CRC16_reset_crc(&crc);
	CRC16_AddToCRC(&crc, 0U);
	CRC16_AddToCRC(&crc, seq);
	CRC16_AddToCRC(&crc, type);
	value = CRC16_GetCRC(&crc);

	RingBufWriteOne(&to->l1_buffer_rx, START_OF_FRAME);
	RingBufWriteOne(&to->l1_buffer_rx, 0U);
	RingBufWriteOne(&to->l1_buffer_rx, seq);
	RingBufWriteOne(&to->l1_buffer_rx, type);
	RingBufWriteOne(&to->l1_buffer_rx, (uint8_t)(value >> 8));
	RingBufWriteOne(&to->l1_buffer_rx, (uint8_t)(value & 0xFFU));
}

static void test_Arq_ShouldDeliverAndAck_WhenLinkIsClean(void)
{
	uint8_t msg[] = "hello";
	uint8_t rx[DATA_BUFFER_SIZE];
	uint8_t len = 0U;
	uint8_t seq = 0xFFU;

	setup();
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)A, msg, 5U, NULL) == 0);
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)A, msg, 5U, NULL) == -1);
	CHECK(run_until_rx(B, rx, &len, &seq) > 0);
	CHECK((len == 5U) && (memcmp(rx, msg, 5U) == 0));
	CHECK(seq == 0U);
	CHECK(run_until_tx_ready(A) > 0);
	CHECK(PARSECS_Get_Link_State((uint8_t)A) == PARSECS_LINK_UP);
	CHECK(retx_of(A) == 0U);
}

static void test_Arq_ShouldRetransmitAfterTimeout_WhenDataFrameIsLost(void)
{
	uint8_t msg[] = "hello";
	uint8_t rx[DATA_BUFFER_SIZE];
	uint8_t len = 0U;
	int ticks;

	setup();
	a_to_b.drop = FRAME_OVERHEAD + 5;
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)A, msg, 5U, NULL) == 0);
	ticks = run_until_rx(B, rx, &len, NULL);
	CHECK(ticks >= (int)ARQ_RTO_ROUNDS);
	CHECK((len == 5U) && (memcmp(rx, msg, 5U) == 0));
	CHECK(run_until_tx_ready(A) > 0);
	CHECK(retx_of(A) == 1U);
	CHECK(PARSECS_Get_Link_State((uint8_t)A) == PARSECS_LINK_UP);
}

static void test_Arq_ShouldRetransmitOnNackBeforeTimeout_WhenCrcIsCorrupted(void)
{
	uint8_t msg[] = "hello";
	uint8_t rx[DATA_BUFFER_SIZE];
	uint8_t len = 0U;
	int ticks;

	setup();
	a_to_b.corrupt_at = 6; /* SOF LEN SEQ TYPE D0 [D1] */
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)A, msg, 5U, NULL) == 0);
	ticks = run_until_rx(B, rx, &len, NULL);
	CHECK((ticks > 0) && (ticks < (int)ARQ_RTO_ROUNDS));
	CHECK((len == 5U) && (memcmp(rx, msg, 5U) == 0));
	CHECK(run_until_tx_ready(A) > 0);
	CHECK(retx_of(A) == 1U);
}

static void test_Arq_ShouldReAckWithoutRedelivering_WhenAckIsLost(void)
{
	uint8_t msg[] = "hello";
	uint8_t rx[DATA_BUFFER_SIZE];
	uint8_t len = 0U;
	int deliveries = 0;
	int t;

	setup();
	b_to_a.drop = FRAME_OVERHEAD; /* the whole ACK frame */
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)A, msg, 5U, NULL) == 0);
	for (t = 0; t < 3000; t++)
	{
		tick();
		if (PARSECS_RECEIVE_APP((uint8_t)B, rx, &len, NULL) == 0)
		{
			deliveries++;
		}
	}
	CHECK(deliveries == 1);
	CHECK(PARSECS_CHECK_APP_TX_READY((uint8_t)A) == 0);
	CHECK(retx_of(A) == 1U);
	CHECK(PARSECS_Get_Link_State((uint8_t)A) == PARSECS_LINK_UP);
}

static void test_Arq_ShouldGoLinkDownWithinBound_WhenPeerIsAbsent(void)
{
	uint8_t msg[] = "hello";
	uint8_t rx[DATA_BUFFER_SIZE];
	uint8_t len = 0U;
	int bound = 0;
	int ticks;
	uint16_t rto = ARQ_RTO_ROUNDS;
	uint8_t i;

	for (i = 0U; i <= ARQ_MAX_RETRIES; i++)
	{
		bound += rto + FRAME_OVERHEAD + 5 + RAW_RING_BUFFER_SIZE;
		rto = (rto * 2U > ARQ_RTO_MAX_ROUNDS) ? ARQ_RTO_MAX_ROUNDS : (uint16_t)(rto * 2U);
	}

	setup();
	peer_absent = true;
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)A, msg, 5U, NULL) == 0);
	ticks = run_until_tx_ready(A);
	printf("    link down after %d rounds (bound %d)\n", ticks, bound);
	CHECK((ticks > 0) && (ticks <= bound));
	CHECK(PARSECS_Get_Link_State((uint8_t)A) == PARSECS_LINK_DOWN);
	CHECK(retx_of(A) == ARQ_MAX_RETRIES);
	CHECK(dropped_of(A) == 1U);

	/* Peer plugged back in: the next frame is a probe and brings the link up. */
	peer_absent = false;
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)A, msg, 5U, NULL) == 0);
	CHECK(run_until_rx(B, rx, &len, NULL) > 0);
	CHECK(run_until_tx_ready(A) > 0);
	CHECK(PARSECS_Get_Link_State((uint8_t)A) == PARSECS_LINK_UP);
}

static void test_Arq_ShouldKeepOrderAcrossSeqWrap(void)
{
	uint8_t rx[DATA_BUFFER_SIZE];
	uint8_t len = 0U;
	uint8_t seq = 0U;
	int i;
	int bad = 0;

	setup();
	for (i = 0; i < 300; i++)
	{
		uint8_t msg[3] = { (uint8_t)i, (uint8_t)(i >> 8), 0x55U };
		if ((PARSECS_TRANSMIT_APP((uint8_t)A, msg, 3U, NULL) != 0) ||
		    (run_until_rx(B, rx, &len, &seq) < 0) ||
		    (len != 3U) || (memcmp(rx, msg, 3U) != 0) || (seq != (uint8_t)i) ||
		    (run_until_tx_ready(A) < 0))
		{
			bad++;
		}
	}
	CHECK(bad == 0);
	CHECK(retx_of(A) == 0U);
}

static void test_Arq_ShouldIgnoreAckWithStaleSeq(void)
{
	uint8_t msg[] = "hello";
	uint8_t rx[DATA_BUFFER_SIZE];
	uint8_t len = 0U;
	int t;

	setup();
	b_to_a.drop = FRAME_OVERHEAD;
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)A, msg, 5U, NULL) == 0);
	CHECK(run_until_rx(B, rx, &len, NULL) > 0);
	for (t = 0; t < 20; t++)
	{
		tick();
	}
	CHECK((slaves[A].STATUS_FLAG & FG_ARQ_WAIT_ACK) != 0);
	inject_frame(&slaves[A], ACK, (uint8_t)(slaves[A].arq_seq + 7U));
	tick();
	CHECK(slaves[A].last_ACKed_packet_sequence_number == (uint8_t)(slaves[A].arq_seq + 7U));
	CHECK(PARSECS_CHECK_APP_TX_READY((uint8_t)A) == -1);
	CHECK(run_until_tx_ready(A) > 0);
	CHECK(retx_of(A) == 1U);
}

static void test_Arq_ShouldNotLoseQueuedData_WhenAckIsPending(void)
{
	uint8_t ping[] = "ping";
	uint8_t pong[] = "pong";
	uint8_t rx[DATA_BUFFER_SIZE];
	uint8_t len = 0U;

	setup();
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)A, ping, 4U, NULL) == 0);
	CHECK(run_until_rx(B, rx, &len, NULL) > 0);
	/* B still owes the ACK for "ping" while its High Level queues the reply. */
	CHECK((slaves[B].STATUS_FLAG & FG_ACK) != 0);
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)B, pong, 4U, NULL) == 0);
	CHECK(run_until_rx(A, rx, &len, NULL) > 0);
	CHECK((len == 4U) && (memcmp(rx, pong, 4U) == 0));
	CHECK(run_until_tx_ready(A) > 0);
	CHECK(run_until_tx_ready(B) > 0);
	CHECK(retx_of(A) == 0U);
	CHECK(retx_of(B) == 0U);
}

static void test_Arq_ShouldNotTimeOut_WhileLongPeerFrameIsInFlight(void)
{
	uint8_t big[DATA_BUFFER_SIZE];
	uint8_t msg[] = "hello";
	uint8_t rx[DATA_BUFFER_SIZE];
	uint8_t len = 0U;
	bool a_got = false;
	bool b_got = false;
	int t;

	memset(big, 0xA5, sizeof(big));
	setup();
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)B, big, DATA_BUFFER_SIZE, NULL) == 0);
	tick();
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)A, msg, 5U, NULL) == 0);
	for (t = 0; (t < MAX_TEST_TICKS) && !(a_got && b_got); t++)
	{
		tick();
		if (PARSECS_RECEIVE_APP((uint8_t)A, rx, &len, NULL) == 0)
		{
			a_got = (len == DATA_BUFFER_SIZE) && (memcmp(rx, big, len) == 0);
		}
		if (PARSECS_RECEIVE_APP((uint8_t)B, rx, &len, NULL) == 0)
		{
			b_got = (len == 5U) && (memcmp(rx, msg, len) == 0);
		}
	}
	CHECK(a_got && b_got);
	CHECK(run_until_tx_ready(A) > 0);
	CHECK(run_until_tx_ready(B) > 0);
	CHECK(retx_of(A) == 0U);
	CHECK(retx_of(B) == 0U);
}

static void test_ResetLink_ShouldClearArqState_AndRejectBadIds(void)
{
	uint8_t msg[] = "hello";

	setup();
	b_to_a.drop = 1000;
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)A, msg, 5U, NULL) == 0);
	while ((slaves[A].STATUS_FLAG & FG_ARQ_WAIT_ACK) == 0)
	{
		tick();
	}
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)A, msg, 5U, NULL) == -1);
	CHECK(PARSECS_Reset_Link((uint8_t)A) == 0);
	CHECK(PARSECS_CHECK_APP_TX_READY((uint8_t)A) == 0);
	CHECK(slaves[A].current_seq_no == 0U);
	CHECK(PARSECS_Get_Link_State((uint8_t)A) == PARSECS_LINK_UP);

	CHECK(PARSECS_Reset_Link(SLAVE_COUNT) == -1);
	CHECK(PARSECS_Get_Link_State(SLAVE_COUNT) == -1);
	CHECK(PARSECS_Get_ARQ_Stats(SLAVE_COUNT, NULL, NULL) == -1);
	CHECK(PARSECS_Reset_Link(SLAVE_COUNT - 1U) == -2);
	CHECK(PARSECS_Get_Link_State(SLAVE_COUNT - 1U) == -2);
	CHECK(PARSECS_CHECK_APP_TX_READY(SLAVE_COUNT) == -1);
}

typedef void (*test_fn)(void);

typedef struct
{
	const char *name;
	test_fn fn;
} test_case_t;

#define TEST_CASE(fn) { #fn, fn }

int main(void)
{
	static const test_case_t tests[] = {
		TEST_CASE(test_Arq_ShouldDeliverAndAck_WhenLinkIsClean),
		TEST_CASE(test_Arq_ShouldRetransmitAfterTimeout_WhenDataFrameIsLost),
		TEST_CASE(test_Arq_ShouldRetransmitOnNackBeforeTimeout_WhenCrcIsCorrupted),
		TEST_CASE(test_Arq_ShouldReAckWithoutRedelivering_WhenAckIsLost),
		TEST_CASE(test_Arq_ShouldGoLinkDownWithinBound_WhenPeerIsAbsent),
		TEST_CASE(test_Arq_ShouldKeepOrderAcrossSeqWrap),
		TEST_CASE(test_Arq_ShouldIgnoreAckWithStaleSeq),
		TEST_CASE(test_Arq_ShouldNotLoseQueuedData_WhenAckIsPending),
		TEST_CASE(test_Arq_ShouldNotTimeOut_WhileLongPeerFrameIsInFlight),
		TEST_CASE(test_ResetLink_ShouldClearArqState_AndRejectBadIds),
	};
	size_t i;

	for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
	{
		int before = failures;
		tests[i].fn();
		printf("%s %s\n", (failures == before) ? "PASS" : "FAIL", tests[i].name);
	}
	printf("%d checks, %d failed\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}
