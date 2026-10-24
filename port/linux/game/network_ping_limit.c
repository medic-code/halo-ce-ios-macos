/*
NETWORK_PING_LIMIT.C

The host's ping limit (network.host_ping_limit): a client machine whose
round trip to the host stays above it is kicked, as the host's kick does
(network_server_manager.c), and may join again. The round trip is the
internet play tunnel's (p2p.c), which pings each peer once a second, as the
server browser's PING column times a host. The host takes one a second and
judges the median of the latest PING_LIMIT_SAMPLES once it has
PING_LIMIT_MINIMUM_SAMPLES, so that one slow ping kicks no one.

- A machine is judged in the lobby, and in its first
  PING_LIMIT_ADMISSION_MILLISECONDS in a game it joined in progress; never
  later in a game, where a kick would take a player out of a match it is
  playing.
- Never the host's own machine (its split screen's players), nor a machine
  the tunnel does not reach: a system link (LAN) machine has no round trip
  timed, so no limit.
- Not while the machine downloads the game's maps (map_share.c), whose
  pieces fill its tunnel and so its round trip.
- The machine is told what a kick tells it (_rejection_code_blacklisted_machine),
  as a reason of its own would change what the machines send; the host's
  notice says why, as a kick's does.
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
	/* the round trips judged, the fewest judged, and how often one is taken */
	PING_LIMIT_SAMPLES = 5,
	PING_LIMIT_MINIMUM_SAMPLES = 3,
	PING_LIMIT_SAMPLE_MILLISECONDS = 1000,
	/* a machine that joined a game in progress is judged this long */
	PING_LIMIT_ADMISSION_MILLISECONDS = 10000,
	/* the longest round trip kept */
	MAXIMUM_PING_LIMIT_MILLISECONDS = 9999,
	/* internet play's stand-in addresses, 100.64.0.0/10 (host byte order) */
	PING_LIMIT_INTERNET_MASK = 0xFFC00000,
	PING_LIMIT_INTERNET_ADDRESS = 0x64400000,
};

/* ---------- structures */

/* a machine slot's latest round trips (milliseconds), the next one's
place, and when one was last taken */
struct ping_limit_machine
{
	short samples[PING_LIMIT_SAMPLES];
	short count;
	short next;
	unsigned long sampled_at;
};

/* ---------- globals */

static struct ping_limit_machine ping_limit_machines[MAXIMUM_PING_LIMIT_MACHINES];

/* ---------- the decision (pure: tools/harness/tests/ping_limit.c) */

/* the median of the round trips (the lower of the middle two of an even
count), NONE for fewer than PING_LIMIT_MINIMUM_SAMPLES */
static long ping_limit_median(
	short const *samples,
	short count)
{
	short sorted[PING_LIMIT_SAMPLES];
	short index;

	if (count < PING_LIMIT_MINIMUM_SAMPLES || count > PING_LIMIT_SAMPLES)
		return NONE;
	for (index = 0; index < count; index++)
	{
		short place = index;

		while (place > 0 && sorted[place - 1] > samples[index])
		{
			sorted[place] = sorted[place - 1];
			place--;
		}
		sorted[place] = samples[index];
	}
	return sorted[(count - 1) / 2];
}

/* whether the round trips put a machine over the limit (milliseconds; 0 or
less: none) */
static boolean ping_limit_over(
	long limit,
	short const *samples,
	short count)
{
	long median = ping_limit_median(samples, count);

	return limit > 0 && median != NONE && median > limit;
}

/* the tunnel's round trip to the machine of the address (host byte order),
NONE for none: not an internet play peer's, or not timed yet */
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

/* (network_server_manager.c) a machine joined the host at the slot: none of
the round trips of the machine before are its */
void network_ping_limit_machine_joined(
	long machine_index)
{
	if (machine_index < 0 || machine_index >= MAXIMUM_PING_LIMIT_MACHINES)
		return;
	ping_limit_machines[machine_index].count = 0;
	ping_limit_machines[machine_index].next = 0;
	ping_limit_machines[machine_index].sampled_at = 0;
}

/* (network_server_manager.c, the host, as it looks at a client machine not
its own) the median round trip, if it is over network.host_ping_limit and
the machine is to be kicked; else NONE. lobby: the game not started;
joined: the milliseconds since the machine joined */
long network_ping_limit_check(
	long machine_index,
	unsigned long address,
	boolean lobby,
	unsigned long joined)
{
	unsigned long now = system_milliseconds();
	long limit;
	long ping;
	long median;

	if (machine_index < 0 || machine_index >= MAXIMUM_PING_LIMIT_MACHINES ||
		now - ping_limit_machines[machine_index].sampled_at < PING_LIMIT_SAMPLE_MILLISECONDS)
	{
		return NONE;
	}
	ping_limit_machines[machine_index].sampled_at = now;
	limit = config_integer("network.host_ping_limit");
	if (limit <= 0 || (!lobby && joined > PING_LIMIT_ADMISSION_MILLISECONDS) ||
		map_share_machine_downloading(machine_index))
	{
		ping_limit_machines[machine_index].count = 0;
		return NONE;
	}
	ping = ping_limit_tunnel_ping(address);
	if (ping == NONE)
		return NONE;
	ping_limit_machines[machine_index].samples[ping_limit_machines[machine_index].next] =
		(short)MIN(ping, (long)MAXIMUM_PING_LIMIT_MILLISECONDS);
	ping_limit_machines[machine_index].next = (ping_limit_machines[machine_index].next + 1) % PING_LIMIT_SAMPLES;
	if (ping_limit_machines[machine_index].count < PING_LIMIT_SAMPLES)
		ping_limit_machines[machine_index].count++;
	if (!ping_limit_over(limit, ping_limit_machines[machine_index].samples, ping_limit_machines[machine_index].count))
		return NONE;
	median = ping_limit_median(ping_limit_machines[machine_index].samples, ping_limit_machines[machine_index].count);
	ping_limit_machines[machine_index].count = 0;
	return median;
}

/* (network_server_manager.c) the limit kicked the machine of these players
(empty: it had none yet) at this round trip: said as network_distributed_kick
says a kick, on every machine's console while the game's clock runs, else on
the host's */
void network_ping_limit_kicked(
	char const *names,
	long ping)
{
	/* (network_distributed.c's MAXIMUM_NOTICE_LENGTH) */
	char notice[160];

	snprintf(notice, sizeof(notice), "%s removed: ping %ld ms, over the host's limit of %ld ms",
		names && names[0] ? names : "A player's machine", ping, config_integer("network.host_ping_limit"));
	if (game_in_progress())
		distributed_send_notice(notice);
	else
	{
		console_warning("%s", notice);
		error(_error_log, "%s", notice);
	}
}
