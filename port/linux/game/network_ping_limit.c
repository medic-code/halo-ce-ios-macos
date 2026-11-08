/*
NETWORK_PING_LIMIT.C

The host's ping limit (network.host_ping_limit): a client machine whose
internet play tunnel round trip (p2p.c) stays above it is kicked, free to
join again. The median of several round trips is judged, so that one slow
ping kicks no one.

- Judged in the lobby and in a machine's first seconds in a game it joined
  in progress, never later: a kick would take a player out of a match.
- A system link (LAN) machine has no round trip timed, so no limit.
- Not while the machine downloads the game's maps (map_share.c), whose
  pieces fill its tunnel and so its round trip.
- The machine is told what a kick tells it (_rejection_code_blacklisted_machine):
  a reason of its own would change what the machines send.
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "network_distributed.h"
#include "map_share.h"
#include "../src/p2p.h"

#include <stdio.h>

/* port_config.c's, cseries_windows.c's, console.c's */
long config_integer(const char *name);
unsigned long system_milliseconds(void);
void console_warning(const char *format, ...);

/* ---------- constants */

enum
{
	MAXIMUM_PING_LIMIT_MACHINES = HALO_PORT_MAXIMUM_NETWORK_MACHINES,
	PING_LIMIT_SAMPLES = 5,
	PING_LIMIT_MINIMUM_SAMPLES = 3,
	PING_LIMIT_SAMPLE_MILLISECONDS = 1000,
	PING_LIMIT_ADMISSION_MILLISECONDS = 10000,
	/* internet play's stand-in addresses, 100.64.0.0/10 (host byte order) */
	PING_LIMIT_INTERNET_MASK = 0xFFC00000,
	PING_LIMIT_INTERNET_ADDRESS = 0x64400000,
};

/* ---------- structures */

struct ping_limit_machine
{
	long samples[PING_LIMIT_SAMPLES];
	unsigned long taken;
	unsigned long sampled_at;
	boolean seen_in_lobby;
};

/* ---------- globals */

static struct ping_limit_machine ping_limit_machines[MAXIMUM_PING_LIMIT_MACHINES];

/* ---------- the decision (pure: tools/harness/tests/ping_limit.c) */

static long ping_limit_median(
	long const *samples,
	long count)
{
	long sorted[PING_LIMIT_SAMPLES];
	long index;

	if (count < PING_LIMIT_MINIMUM_SAMPLES)
		return NONE;
	for (index = 0; index < count; index++)
	{
		long place = index;

		while (place > 0 && sorted[place - 1] > samples[index])
		{
			sorted[place] = sorted[place - 1];
			place--;
		}
		sorted[place] = samples[index];
	}
	return sorted[(count - 1) / 2];
}

static boolean ping_limit_over(
	long limit,
	long median)
{
	return limit > 0 && median != NONE && median > limit;
}

/* address: host byte order; the tunnel names peers in network byte order */
static long ping_limit_tunnel_ping(
	unsigned long address)
{
	unsigned char identifier[6];
	unsigned char key_hash[P2P_PLAYER_KEY_HASH_SIZE];
	unsigned long network;
	int ping;

	if ((address & PING_LIMIT_INTERNET_MASK) != PING_LIMIT_INTERNET_ADDRESS)
		return NONE;
	network = (address >> 24) | ((address >> 8) & 0xFF00) | ((address << 8) & 0xFF0000) | (address << 24);
	if (!p2p_peer_key_hash(network, identifier, key_hash))
		return NONE;
	ping = p2p_peer_ping(identifier);
	return ping < 0 ? NONE : (long)ping;
}

/* ---------- public code */

void network_ping_limit_machine_joined(
	long machine_index)
{
	if (machine_index < 0 || machine_index >= MAXIMUM_PING_LIMIT_MACHINES)
		return;
	ping_limit_machines[machine_index].taken = 0;
	ping_limit_machines[machine_index].sampled_at = 0;
	ping_limit_machines[machine_index].seen_in_lobby = FALSE;
}

long network_ping_limit_check(
	long machine_index,
	unsigned long address,
	boolean lobby,
	unsigned long joined)
{
	struct ping_limit_machine *machine;
	unsigned long now = system_milliseconds();
	long limit;
	long ping;
	long median;

	if (machine_index < 0 || machine_index >= MAXIMUM_PING_LIMIT_MACHINES)
		return NONE;
	machine = &ping_limit_machines[machine_index];
	if (lobby)
		machine->seen_in_lobby = TRUE;
	if (now - machine->sampled_at < PING_LIMIT_SAMPLE_MILLISECONDS)
		return NONE;
	machine->sampled_at = now;
	limit = config_integer("network.host_ping_limit");
	if (limit <= 0 || (!lobby && (machine->seen_in_lobby || joined > PING_LIMIT_ADMISSION_MILLISECONDS)) ||
		map_share_machine_downloading(machine_index))
	{
		machine->taken = 0;
		return NONE;
	}
	ping = ping_limit_tunnel_ping(address);
	if (ping == NONE)
		return NONE;
	machine->samples[machine->taken++ % PING_LIMIT_SAMPLES] = ping;
	median = ping_limit_median(machine->samples, (long)MIN(machine->taken, (unsigned long)PING_LIMIT_SAMPLES));
	if (!ping_limit_over(limit, median))
		return NONE;
	machine->taken = 0;
	return median;
}

void network_ping_limit_kicked(
	char const *names,
	long ping)
{
	/* (network_distributed.c's MAXIMUM_NOTICE_LENGTH) */
	char notice[160];

	snprintf(notice, sizeof(notice), "%s removed: ping %ld ms, over the host's limit of %ld ms",
		names[0] ? names : "A player's machine", ping, config_integer("network.host_ping_limit"));
	if (game_in_progress())
		distributed_send_notice(notice);
	else
	{
		console_warning("%s", notice);
		error(_error_log, "%s", notice);
	}
}
