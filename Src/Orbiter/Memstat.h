// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#ifndef __MEMSTAT_H
#define __MEMSTAT_H

// windows.h/psapi.h left out: the working set is read from /proc/self/statm

class MemStat {
public:
    MemStat ();
    ~MemStat ();

    long HeapUsage ();

private:
    // hLib/bLib/hProc/pGetProcessMemoryInfo left out: /proc needs no library or process handle
    bool active;
};

#endif // !__MEMSTAT_H
