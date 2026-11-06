//! \file	PARSECS_HotPlug.c
//!
//! \brief	PARSECS slave hot-plug (master only)
//!
//! Every slot registered with \ref PARSECS_Add_Slave is in one of three states:
//! - \ref PARSECS_SLAVE_ABSENT: not polled. Now and then a Low Level tick is spent on a probe round
//!   to its address instead (dummy byte out, byte in discarded, no ring touched).
//! - \ref PARSECS_SLAVE_LINKING: answered \ref HOTPLUG_ADD_OK_ROUNDS probes in a row. It is back in
//!   the round-robin and its link is being restarted with \ref PARSECS_Link_Sync_Start.
//! - \ref PARSECS_SLAVE_ACTIVE: the link reset CMD was ACKed. The High Level may use the board.
//!
//! A LINKING or ACTIVE slave that fails \ref HOTPLUG_REMOVE_FAIL_ROUNDS rounds in a row (or whose link
//! reset CMD is dropped by the ARQ) goes back to ABSENT. The round-robin in \ref PARSECS_LowLevelTask
//! only walks LINKING and ACTIVE slaves, so with N of them each one gets every Nth tick: no tick is
//! spent on a board that is not there, apart from one probe every \ref HOTPLUG_PROBE_PERIOD_TICKS.
//!
//! The round results come from the Layer 1 ISR as two saturating streak counters in SLAVE. Everything
//! here runs in the Low Level task, before Layer 1, and is platform independent.
//! \addtogroup PARSECS_RT
//! @{
//! \addtogroup PARSECS-Low-Level-Substack
//! @{
#include "PARSECS_HotPlug.h"
#include "PARSECS_LowLevelAPI.h"

#ifdef SPI_MASTER

static uint8_t probe_ticks = 0U;
static uint8_t probe_next = 0U;
static int8_t probe_sticky = -1;

static void count_active(void)
{
	uint8_t i;
	uint8_t n = 0U;
	for (i = 0U; i < SLAVE_COUNT; i++)
	{
		if ((slaves[i].slave_enabled == 1) && (slaves[i].hp_state != PARSECS_SLAVE_ABSENT))
		{
			n++;
		}
	}
	number_of_active_slaves = n;
}

static void take_out(SLAVE *slave)
{
	if (slave->hp_state == PARSECS_SLAVE_ACTIVE)
	{
		slave->hp_removed++;
	}
	slave->hp_state = PARSECS_SLAVE_ABSENT;
	slave->hp_ok_streak = 0U;
	slave->hp_fail_streak = 0U;
}

//! Resets the probe scheduler. Slot states are set by \ref PARSECS_Add_Slave.
//! \return None
void PARSECS_HotPlug_Init(void)
{
	probe_ticks = 0U;
	probe_next = 0U;
	probe_sticky = -1;
	count_active();
}

//! Applies the round results of the previous ticks to the slave states. Called once per Low Level tick.
//! \return None
void PARSECS_HotPlug_Task(void)
{
	uint8_t i;

	for (i = 0U; i < SLAVE_COUNT; i++)
	{
		SLAVE *slave = &slaves[i];
		if (slave->slave_enabled != 1)
		{
			continue;
		}

		switch (slave->hp_state)
		{
		case PARSECS_SLAVE_ABSENT:
			if (slave->hp_ok_streak >= HOTPLUG_ADD_OK_ROUNDS)
			{
				slave->hp_fail_streak = 0U;
				if (PARSECS_Link_Sync_Start(i) == 0)
				{
					slave->hp_state = PARSECS_SLAVE_LINKING;
				}
			}
			break;
		case PARSECS_SLAVE_LINKING:
			if ((slave->hp_fail_streak >= HOTPLUG_REMOVE_FAIL_ROUNDS) ||
			    (slave->link_state == PARSECS_LINK_DOWN))
			{
				// gone again, or answers the address but never ACKs the CMD (older firmware)
				take_out(slave);
			}
			else if (!slave->link_sync_pending)
			{
				slave->hp_state = PARSECS_SLAVE_ACTIVE;
				slave->hp_added++;
			}
			break;
		case PARSECS_SLAVE_ACTIVE:
		default:
			if (slave->hp_fail_streak >= HOTPLUG_REMOVE_FAIL_ROUNDS)
			{
				take_out(slave);
			}
			break;
		}
	}
	count_active();
}

//! Picks the absent slave to probe on this tick, if any.
//!
//! With no slave polled every tick probes; otherwise one tick in \ref HOTPLUG_PROBE_PERIOD_TICKS.
//! A slave that answered its last probe is probed again on the next probe tick, so a board that was
//! just plugged in is confirmed in \ref HOTPLUG_ADD_OK_ROUNDS probe ticks instead of waiting for the
//! rotation to come back to it.
//! \return The slave to probe, or NULL for a normal tick
SLAVE *PARSECS_HotPlug_Probe_Target(void)
{
	uint8_t k;

	if (number_of_active_slaves > 0U)
	{
		if (++probe_ticks < HOTPLUG_PROBE_PERIOD_TICKS)
		{
			return NULL;
		}
	}
	probe_ticks = 0U;

	if ((probe_sticky >= 0) && (slaves[probe_sticky].slave_enabled == 1) &&
	    (slaves[probe_sticky].hp_state == PARSECS_SLAVE_ABSENT) && (slaves[probe_sticky].hp_ok_streak > 0U))
	{
		return &slaves[probe_sticky];
	}

	for (k = 0U; k < SLAVE_COUNT; k++)
	{
		uint8_t i = (uint8_t)((probe_next + k) % SLAVE_COUNT);
		if ((slaves[i].slave_enabled == 1) && (slaves[i].hp_state == PARSECS_SLAVE_ABSENT))
		{
			probe_next = (uint8_t)((i + 1U) % SLAVE_COUNT);
			probe_sticky = (int8_t)i;
			return &slaves[i];
		}
	}
	probe_sticky = -1;
	return NULL;
}

//! \return The number of slaves in the round-robin (LINKING or ACTIVE)
uint8_t PARSECS_HotPlug_Active_Count(void)
{
	return number_of_active_slaves;
}

#endif

//! @}

//! @}
