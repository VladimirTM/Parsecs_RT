/*
 * Host test for the slave hot-plug of the master.
 *
 * The real PARSECS_LowLevelTask, PARSECS_HotPlug, Layer 2, Layer 3 and the Low
 * Level API run for the master. Layer 1 is replaced by a stand-in with the same
 * contract as the I2C one: one byte each way per round, a data round to an
 * unplugged board fails (TX byte stays queued), a probe round sends a dummy
 * byte, drops the byte it reads and only updates the hot-plug streaks.
 *
 * Each board is a second SLAVE struct (its own stack, slave_enabled cleared so
 * the master's hot-plug code ignores it) whose Layer 2/3 run every tick while
 * it is powered.
 */

#include <stdio.h>
#include <string.h>

#include "PARSECS_LowLevelAPI.h"
#include "PARSECS_LowLevelTask.h"
#include "PARSECS_HotPlug.h"
#include "PARSECS_Layer1.h"
#include "PARSECS_Layer2.h"
#include "PARSECS_Layer3.h"

#define MAX_BOARDS      3
#define MAX_TEST_TICKS  20000

typedef struct
{
	int8_t master_slot;     /* slot the master polls for this board */
	int8_t node;            /* the board's own stack */
	bool plugged;           /* on the bus */
	bool powered;           /* its stack runs */
	uint32_t data_rounds;
	uint32_t probe_rounds;
} board_t;

static board_t boards[MAX_BOARDS];
static int board_count;
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

static board_t *board_of(SLAVE *slave)
{
	int i;
	for (i = 0; i < board_count; i++)
	{
		if (&slaves[boards[i].master_slot] == slave)
		{
			return &boards[i];
		}
	}
	return NULL;
}

static void hp_ok(SLAVE *s)   { s->hp_fail_streak = 0U; if (s->hp_ok_streak < 255U) s->hp_ok_streak++; }
static void hp_fail(SLAVE *s) { s->hp_ok_streak = 0U; if (s->hp_fail_streak < 255U) s->hp_fail_streak++; }

/* ---- Layer 1 stand-in (master side) ---- */

void PARSECS_Layer1_Init(void)
{
}

void PARSECS_LAYER1(SLAVE *slave)
{
	board_t *b = board_of(slave);
	SLAVE *peer;
	uint8_t out = 0x00U;
	uint8_t in = 0x00U;
	bool have_tx = !RingBufEmpty(&slave->l1_buffer_tx);

	slave->l1_rounds++;
	if ((b == NULL) || !b->plugged || !b->powered)
	{
		hp_fail(slave);
		return;
	}
	b->data_rounds++;
	peer = &slaves[b->node];
	if (have_tx)
	{
		out = RingBufReadOne(&slave->l1_buffer_tx);
	}
	if (!RingBufEmpty(&peer->l1_buffer_tx))
	{
		in = RingBufReadOne(&peer->l1_buffer_tx);
	}
	if (!RingBufFull(&peer->l1_buffer_rx))
	{
		RingBufWriteOne(&peer->l1_buffer_rx, out);
	}
	if (!RingBufFull(&slave->l1_buffer_rx))
	{
		RingBufWriteOne(&slave->l1_buffer_rx, in);
	}
	peer->l1_rounds++;
	hp_ok(slave);
}

void PARSECS_LAYER1_Probe(SLAVE *slave)
{
	board_t *b = board_of(slave);
	SLAVE *peer;

	if ((b == NULL) || !b->plugged || !b->powered)
	{
		hp_fail(slave);
		return;
	}
	b->probe_rounds++;
	peer = &slaves[b->node];
	/* like the real probe: the board clocks out one of its bytes, which is lost */
	if (!RingBufEmpty(&peer->l1_buffer_tx))
	{
		(void)RingBufReadOne(&peer->l1_buffer_tx);
	}
	if (!RingBufFull(&peer->l1_buffer_rx))
	{
		RingBufWriteOne(&peer->l1_buffer_rx, 0x00U);
	}
	peer->l1_rounds++;
	hp_ok(slave);
}

bool PARSECS_Layer1_HoldLinesLow(void) { return true; }
void PARSECS_Layer1_ReleaseLines(void) { }

/* ---- harness ---- */

static void tick(void)
{
	int i;
	PARSECS_LowLevelTask();
	for (i = 0; i < board_count; i++)
	{
		SLAVE *node = &slaves[boards[i].node];
		if (!boards[i].powered)
		{
			continue;
		}
		PARSECS_TRANSMIT_LAYER3(node);
		PARSECS_TRANSMIT_LAYER2(node);
		PARSECS_RECEIVE_LAYER2(node);
		PARSECS_RECEIVE_LAYER3(node);
	}
}

/* Registers n boards; every board starts plugged and powered. */
static void setup(int n)
{
	int i;
	PARSECS_LowLevelTaskInit();
	board_count = n;
	for (i = 0; i < n; i++)
	{
		memset(&boards[i], 0, sizeof(boards[i]));
		boards[i].master_slot = PARSECS_Add_Slave(NULL, NULL);
		boards[i].plugged = true;
		boards[i].powered = true;
	}
	for (i = 0; i < n; i++)
	{
		boards[i].node = PARSECS_Add_Slave(NULL, NULL);
	}
	for (i = 0; i < n; i++)
	{
		/* the board's own stack is not something the master polls (cleared
		 * after all slots are taken, or Add_Slave would hand it out again) */
		slaves[boards[i].node].slave_enabled = 0;
	}
	PARSECS_HotPlug_Init();
}

static int8_t state_of(int i)
{
	return PARSECS_Get_Slave_State((uint8_t)boards[i].master_slot);
}

static int run_until_state(int i, int8_t state)
{
	int t;
	for (t = 1; t <= MAX_TEST_TICKS; t++)
	{
		tick();
		if (state_of(i) == state)
		{
			return t;
		}
	}
	return -1;
}

static int run_until_rx(int8_t node, uint8_t *data, uint8_t *len)
{
	int t;
	for (t = 1; t <= MAX_TEST_TICKS; t++)
	{
		tick();
		if (PARSECS_RECEIVE_APP((uint8_t)node, data, len, NULL) == 0)
		{
			return t;
		}
	}
	return -1;
}

static void run(int ticks)
{
	int t;
	for (t = 0; t < ticks; t++)
	{
		tick();
	}
}

/* ---- tests ---- */

static void test_HotPlug_ShouldFindBothBoards_WhenPresentAtBoot(void)
{
	int t0;
	int t1;

	setup(2);
	CHECK(state_of(0) == PARSECS_SLAVE_ABSENT);
	CHECK(PARSECS_HotPlug_Active_Count() == 0U);
	t0 = run_until_state(0, PARSECS_SLAVE_ACTIVE);
	t1 = run_until_state(1, PARSECS_SLAVE_ACTIVE);
	printf("    comm ACTIVE after %d ticks, mobility %d ticks later\n", t0, t1);
	CHECK((t0 > 0) && (t0 < 200));
	CHECK((t1 >= 0) && (t1 < 300));
	CHECK(PARSECS_HotPlug_Active_Count() == 2U);
	CHECK(PARSECS_Get_Link_State((uint8_t)boards[0].master_slot) == PARSECS_LINK_UP);
}

static void test_HotPlug_ShouldGiveAllTicksToTheRest_WhenBoardIsPulled(void)
{
	uint32_t r0;
	uint32_t r1;
	int t;
	uint32_t added = 0U;
	uint32_t removed = 0U;

	setup(2);
	CHECK(run_until_state(0, PARSECS_SLAVE_ACTIVE) > 0);
	CHECK(run_until_state(1, PARSECS_SLAVE_ACTIVE) >= 0);

	boards[1].plugged = false;
	t = run_until_state(1, PARSECS_SLAVE_ABSENT);
	printf("    pulled board out of the round-robin after %d ticks\n", t);
	CHECK((t > 0) && (t <= (int)(2U * HOTPLUG_REMOVE_FAIL_ROUNDS + 4U)));
	CHECK(PARSECS_HotPlug_Active_Count() == 1U);
	CHECK(state_of(0) == PARSECS_SLAVE_ACTIVE);

	/* comm now gets every tick except one probe in HOTPLUG_PROBE_PERIOD_TICKS */
	r0 = slaves[boards[0].master_slot].l1_rounds;
	r1 = slaves[boards[1].master_slot].l1_rounds;
	run(2000);
	r0 = slaves[boards[0].master_slot].l1_rounds - r0;
	r1 = slaves[boards[1].master_slot].l1_rounds - r1;
	printf("    over 2000 ticks: comm %lu rounds, pulled board %lu data rounds\n",
	       (unsigned long)r0, (unsigned long)r1);
	CHECK(r0 == (2000U - (2000U / HOTPLUG_PROBE_PERIOD_TICKS)));
	CHECK(r1 == 0U);
	CHECK(PARSECS_Get_HotPlug_Stats((uint8_t)boards[1].master_slot, &added, &removed) == 0);
	CHECK((added == 1U) && (removed == 1U));
}

static void test_HotPlug_ShouldNotProbe_WhenEveryBoardIsActive(void)
{
	uint32_t probes;

	setup(2);
	CHECK(run_until_state(0, PARSECS_SLAVE_ACTIVE) > 0);
	CHECK(run_until_state(1, PARSECS_SLAVE_ACTIVE) >= 0);
	probes = boards[0].probe_rounds + boards[1].probe_rounds;
	run(1000);
	CHECK(boards[0].probe_rounds + boards[1].probe_rounds == probes);
	/* two boards share the ticks evenly */
	CHECK(boards[0].data_rounds > 0U);
}

static void test_HotPlug_ShouldRegisterNewBoard_WhilePolling(void)
{
	uint8_t msg[] = "ping";
	uint8_t rx[DATA_BUFFER_SIZE];
	uint8_t len = 0U;
	int t;

	setup(2);
	boards[1].plugged = false;
	CHECK(run_until_state(0, PARSECS_SLAVE_ACTIVE) > 0);
	run(500);
	CHECK(state_of(1) == PARSECS_SLAVE_ABSENT);

	boards[1].plugged = true;
	t = run_until_state(1, PARSECS_SLAVE_ACTIVE);
	printf("    plugged board ACTIVE after %d ticks\n", t);
	/* worst case: wait for the rotation, then HOTPLUG_ADD_OK_ROUNDS probes, then the CMD round trip */
	CHECK((t > 0) && (t <= (int)(HOTPLUG_PROBE_PERIOD_TICKS * (HOTPLUG_ADD_OK_ROUNDS + 1U) + 200U)));
	CHECK(PARSECS_HotPlug_Active_Count() == 2U);

	CHECK(PARSECS_TRANSMIT_APP((uint8_t)boards[1].master_slot, msg, 4U, NULL) == 0);
	CHECK(run_until_rx(boards[1].node, rx, &len) > 0);
	CHECK((len == 4U) && (memcmp(rx, msg, 4U) == 0));
}

static void test_HotPlug_ShouldIgnoreSingleAnswer_WhenProbeIsFlaky(void)
{
	int t;

	setup(1);
	boards[0].plugged = false;
	run(50);
	for (t = 0; t < 400; t++)
	{
		/* answers one probe in four: never HOTPLUG_ADD_OK_ROUNDS in a row */
		boards[0].plugged = ((t % 4) == 0);
		tick();
		CHECK(state_of(0) == PARSECS_SLAVE_ABSENT);
	}
}

/* The board stayed powered while it was off the bus: it still has an unACKed
 * response in its TX ring and remembers the old SEQs. After it comes back the
 * master must not deliver the stale response, and the board must take the
 * master's next frame even though the master restarted its SEQ. */
static void test_HotPlug_ShouldResyncStaleBoard_WhenReplugged(void)
{
	uint8_t req[] = "req";
	uint8_t old[] = "stale";
	uint8_t fresh[] = "new";
	uint8_t rx[DATA_BUFFER_SIZE];
	uint8_t len = 0U;
	int8_t m;
	int8_t n;
	uint32_t epoch;
	int t;
	bool stale_seen = false;

	setup(1);
	m = boards[0].master_slot;
	n = boards[0].node;
	CHECK(run_until_state(0, PARSECS_SLAVE_ACTIVE) > 0);
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)m, req, 3U, NULL) == 0);
	CHECK(run_until_rx(n, rx, &len) > 0);
	run(50);

	/* board answers, then is pulled before the answer leaves */
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)n, old, 5U, NULL) == 0);
	tick();
	boards[0].plugged = false;
	CHECK(run_until_state(0, PARSECS_SLAVE_ABSENT) > 0);
	run(300);
	epoch = PARSECS_Get_Link_Epoch((uint8_t)n);

	boards[0].plugged = true;
	for (t = 0; (t < MAX_TEST_TICKS) && (state_of(0) != PARSECS_SLAVE_ACTIVE); t++)
	{
		tick();
		if (PARSECS_RECEIVE_APP((uint8_t)m, rx, &len, NULL) == 0)
		{
			stale_seen = true;
		}
	}
	CHECK(state_of(0) == PARSECS_SLAVE_ACTIVE);
	CHECK(PARSECS_Get_Link_Epoch((uint8_t)n) != epoch);
	/* the old answer may still trickle in: it must not reach the High Level */
	for (t = 0; t < 2000; t++)
	{
		tick();
		if (PARSECS_RECEIVE_APP((uint8_t)m, rx, &len, NULL) == 0)
		{
			stale_seen = true;
		}
	}
	CHECK(!stale_seen);

	CHECK(PARSECS_TRANSMIT_APP((uint8_t)m, fresh, 3U, NULL) == 0);
	CHECK(run_until_rx(n, rx, &len) > 0);
	CHECK((len == 3U) && (memcmp(rx, fresh, 3U) == 0));

	/* and the board can answer on the new link */
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)n, old, 5U, NULL) == 0);
	CHECK(run_until_rx(m, rx, &len) > 0);
	CHECK((len == 5U) && (memcmp(rx, old, 5U) == 0));
}

/* A board that ACKs its address but whose stack never answers (old firmware
 * without the link reset CMD) must not stay LINKING forever. */
static void test_HotPlug_ShouldDropBoard_WhenLinkResetIsNeverAcked(void)
{
	int t;
	int8_t m;

	setup(1);
	m = boards[0].master_slot;
	boards[0].plugged = true;
	/* bus side answers, stack does not run: emulate by never running its layers */
	for (t = 0; (t < MAX_TEST_TICKS) && (state_of(0) != PARSECS_SLAVE_LINKING); t++)
	{
		PARSECS_LowLevelTask();
	}
	CHECK(state_of(0) == PARSECS_SLAVE_LINKING);
	for (t = 0; (t < MAX_TEST_TICKS) && (state_of(0) == PARSECS_SLAVE_LINKING); t++)
	{
		PARSECS_LowLevelTask();
	}
	printf("    unanswered link reset gave up after %d ticks\n", t);
	CHECK(state_of(0) == PARSECS_SLAVE_ABSENT);
	CHECK(slaves[m].hp_added == 0U);
	CHECK(PARSECS_HotPlug_Active_Count() == 0U);
}

static void test_HotPlug_ShouldFindBoard_WhenPluggedIntoEmptyBus(void)
{
	int t;

	setup(2);
	boards[0].plugged = false;
	boards[1].plugged = false;
	run(1000);
	CHECK(PARSECS_HotPlug_Active_Count() == 0U);
	boards[1].plugged = true;
	t = run_until_state(1, PARSECS_SLAVE_ACTIVE);
	printf("    board on an empty bus ACTIVE after %d ticks\n", t);
	CHECK((t > 0) && (t < 100));
	CHECK(state_of(0) == PARSECS_SLAVE_ABSENT);
}

/* Both boards answer at the same time, so the master assembles two frames
 * byte by byte in alternation. Neither may be corrupted by the other. */
static void test_HotPlug_ShouldReceiveBothBoards_WhenTheyAnswerTogether(void)
{
	uint8_t a[40];
	uint8_t b[40];
	uint8_t rx[DATA_BUFFER_SIZE];
	uint8_t len = 0U;
	int t;
	int got = 0;
	uint32_t retx0 = 0U;
	uint32_t retx1 = 0U;

	setup(2);
	CHECK(run_until_state(0, PARSECS_SLAVE_ACTIVE) > 0);
	CHECK(run_until_state(1, PARSECS_SLAVE_ACTIVE) >= 0);
	memset(a, 0xA5, sizeof(a));
	memset(b, 0x5A, sizeof(b));
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)boards[0].node, a, sizeof(a), NULL) == 0);
	CHECK(PARSECS_TRANSMIT_APP((uint8_t)boards[1].node, b, sizeof(b), NULL) == 0);
	for (t = 0; (t < 2000) && (got < 2); t++)
	{
		tick();
		if (PARSECS_RECEIVE_APP((uint8_t)boards[0].master_slot, rx, &len, NULL) == 0)
		{
			CHECK((len == sizeof(a)) && (memcmp(rx, a, sizeof(a)) == 0));
			got++;
		}
		if (PARSECS_RECEIVE_APP((uint8_t)boards[1].master_slot, rx, &len, NULL) == 0)
		{
			CHECK((len == sizeof(b)) && (memcmp(rx, b, sizeof(b)) == 0));
			got++;
		}
	}
	CHECK(got == 2);
	retx0 = slaves[boards[0].node].arq_retransmissions;
	retx1 = slaves[boards[1].node].arq_retransmissions;
	printf("    retransmissions: %lu + %lu\n", (unsigned long)retx0, (unsigned long)retx1);
	CHECK((retx0 == 0U) && (retx1 == 0U));
}

int main(void)
{
#define RUN(test) do { int before = failures; test(); printf("%s %s\n", (failures == before) ? "PASS" : "FAIL", #test); } while (0)
	RUN(test_HotPlug_ShouldFindBothBoards_WhenPresentAtBoot);
	RUN(test_HotPlug_ShouldGiveAllTicksToTheRest_WhenBoardIsPulled);
	RUN(test_HotPlug_ShouldNotProbe_WhenEveryBoardIsActive);
	RUN(test_HotPlug_ShouldRegisterNewBoard_WhilePolling);
	RUN(test_HotPlug_ShouldIgnoreSingleAnswer_WhenProbeIsFlaky);
	RUN(test_HotPlug_ShouldResyncStaleBoard_WhenReplugged);
	RUN(test_HotPlug_ShouldDropBoard_WhenLinkResetIsNeverAcked);
	RUN(test_HotPlug_ShouldFindBoard_WhenPluggedIntoEmptyBus);
	RUN(test_HotPlug_ShouldReceiveBothBoards_WhenTheyAnswerTogether);
	printf("%d checks, %d failed\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}
