// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#include "Memstat.h"
#include <stdio.h>
#include <unistd.h>

// Psapi.dll load left out: GetProcessMemoryInfo's WorkingSetSize is the resident set, field 2 of /proc/self/statm

MemStat::MemStat ()
{
	FILE *f = fopen ("/proc/self/statm", "r");
	active = (f != NULL);
	if (f) fclose (f);
}

MemStat::~MemStat ()
{
}

long MemStat::HeapUsage ()
{
	if (active) {
		long size = 0, resident = 0;
		FILE *f = fopen ("/proc/self/statm", "r");
		if (!f) return 0;
		int n = fscanf (f, "%ld %ld", &size, &resident);
		fclose (f);
		return (n == 2 ? resident * sysconf (_SC_PAGESIZE) : 0);
	} else return 0;
}
