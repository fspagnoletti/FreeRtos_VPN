#include "wireguard-platform.h"

#include <stdlib.h>
#include <time.h>
#include "crypto.h"
#include "lwip/sys.h"

// This file contains a sample Wireguard platform integration

/// Unix time (seconds from 1/1/1970) of the last sys_now() overflow (which happens every 49 days: 32 bit millisecond count)
time_t wgstart = 0;
/// last_milliseconds is used to detect sys_now() overflow (upon which Unix time() is called again to avoid the timestamp going backwards)
uint64_t last_milliseconds = 0;

// DO NOT USE THIS FUNCTION - IMPLEMENT A BETTER RANDOM BYTE GENERATOR IN YOUR IMPLEMENTATION
void wireguard_random_bytes(void *bytes, size_t size) {
	int x;
	uint8_t *out = (uint8_t *)bytes;
	for (x=0; x < size; x++) {
		out[x] = rand() % 0xFF;
	}
}

uint32_t wireguard_sys_now() {
	// Default to the LwIP system time
	return sys_now();
}

// CHANGE THIS TO GET THE ACTUAL UNIX TIMESTMP IN MILLIS - HANDSHAKES WILL FAIL IF THIS DOESN'T INCREASE EACH TIME CALLED
void wireguard_tai64n_now(uint8_t *output) {
	// See https://cr.yp.to/libtai/tai64.html
	// 64 bit seconds from 1970 = 8 bytes
	// 32 bit nano seconds from current second

	uint64_t millis = sys_now();

	// Split into seconds offset + nanos
	//uint64_t seconds = 0x400000000000000aULL + (millis / 1000); // start from 2^62 = 01/01/1970 00:00:00 (same epoch as linux time_t: https://dns.cr.yp.narkive.com/oehRNb17/tai64-timestamp-utility )
	//uint64_t seconds = 0x4000000000000000ULL + (millis / 1000); // start from 2^62 = 01/01/1970 00:00:00 (same epoch as linux time_t: https://dns.cr.yp.narkive.com/oehRNb17/tai64-timestamp-utility )
	uint64_t seconds = (millis / 1000); // seconds from last sys_now() overflow
	uint32_t nanos = (millis % 1000) * 1000000;

	// Technically not a great implementation:
	//   time() could make a system call
	//   which would be best avoided, or at least done
	//   inside a critical section (disable interrupts,
	//   done by masking signals in the linux port).
	// It works anyway, which is enough for a simulation.
	// Also doing calling time() only once reduces the risk.
	// Note that time here is UTC (ok for the tai64 timestamp).
	if(wgstart == 0 || last_milliseconds > millis)
	{
		time(&wgstart);     // get current unix time
		wgstart -= seconds; // subtract seconds from sys_now() to get time of last sys_now() overflow
	}
	seconds += (uint64_t) wgstart;
	seconds += 0x4000000000000000ULL; // tai64 offset 2^62 corresponds to 01/01/1970 00:00:00 (linux time_t epoch) (see https://dns.cr.yp.narkive.com/oehRNb17/tai64-timestamp-utility )

	U64TO8_BIG(output + 0, seconds);
	U32TO8_BIG(output + 8, nanos);

	last_milliseconds = millis;
}

bool wireguard_is_under_load() {
	return false;
}

