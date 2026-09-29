// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#define __LOG_CPP

#include <string.h>
#include <fstream>
#include <cstdarg>
#include <cerrno>
#include <chrono>
#include <link.h> // dl_iterate_phdr lists the loaded objects (EnumProcessModules counterpart)
#include "Log.h"
#include "Orbiter.h"

using namespace std;

extern char DBG_MSG[256];
extern TimeData td;

static char logname[256] = "Orbiter.log";
static char logs[256] = "";
static bool finelog = false;
static std::chrono::steady_clock::time_point t0; // timeGetTime() counterpart

static LogOutFunc logOut = 0;

void InitLog (const char *logfile, bool append)
{
	strcpy (logname, logfile);
	ofstream ofs (logname, append ? ios::app : ios::out);
	ofs << "**** " << logname << endl;
	t0 = std::chrono::steady_clock::now();
}

void SetLogOutFunc(LogOutFunc func)
{
	logOut = func;
}

void SetLogVerbosity (bool verbose)
{
	finelog = verbose;
}

void LogOut (const char *msg, ...)
{
	va_list ap;
	va_start (ap, msg);
	LogOutVA(msg, ap);
	va_end (ap);
}

void LogOutVA(const char *format, va_list ap)
{
	FILE *f = fopen(logname, "a+"); // "t" (MS text mode) left out: Linux streams have no text mode
	fprintf(f, "%010.3f: ", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
	va_list ap2;
	va_copy(ap2, ap); // a va_list can't be read twice on x86-64 Linux (it can on Windows)
	vfprintf(f, format, ap2);
	va_end(ap2);
	fputc('\n', f);
	fclose(f);
	if (logOut) {
		vsnprintf(logs, 255, format, ap);
		(*logOut)(logs);
	}
}

void LogOutFine (const char *msg, ...)
{
	if (finelog) {
		va_list ap;
		va_start (ap, msg);
		FILE *f = fopen (logname, "a+"); // "t" left out, see LogOutVA
		fprintf (f, "%010.3f: ", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
		va_list ap2;
		va_copy (ap2, ap); // see LogOutVA
		vfprintf (f, msg, ap2);
		va_end (ap2);
		fputc ('\n', f);
		fclose (f);
		if (logOut) {
			vsnprintf (logs, 255, msg, ap);
			(*logOut)(logs);
		}
		va_end (ap);
	}
}

void LogOut ()
{
	LogOut (logs);
}

void LogOut_Error (const char *func, const char *file, int line, const char *msg, ...)
{
	va_list ap;
	va_start (ap, msg);
	LogOut_ErrorVA(func, file, line, msg, ap);
	va_end(ap);
}

void LogOut_Error_Start()
{
	LogOut("============================ ERROR: ===========================");
}

void LogOut_Error_End()
{
	LogOut("===============================================================");
}

void LogOut_Warning_Start()
{
	LogOut("--------------------------- WARNING: --------------------------");
}

void LogOut_Warning_End()
{
	LogOut("---------------------------------------------------------------");
}

void LogOut_Obsolete_Start()
{
	LogOut("-------------------------- OBSOLETE: --------------------------");
}

void LogOut_Location(const char* func, const char* file, int line)
{
	LogOut("[%s | %s | %d]", func, file, line);

}

void LogOut_ErrorVA(const char *func, const char *file, int line, const char *msg, va_list ap)
{
	LogOut_Error_Start();
	LogOutVA(msg, ap);
	LogOut_Location(func, file, line);
	LogOut_Error_End();
}

void LogOut_WarningVA(const char* func, const char* file, int line, const char* msg, va_list ap)
{
	LogOut_Warning_Start();
	LogOutVA(msg, ap);
	LogOut_Location(func, file, line);
	LogOut_Warning_End();
}

void LogOut_LastError (const char *func, const char *file, int line)
{
	int err = errno; // GetLastError/FormatMessage counterpart
	LogOut_Error (func, file, line, "%s", strerror (err));
}

// LogOut_DDErr left out: no DirectDraw on Linux

void LogOut_DIErr (int err, const char *func, const char *file, int line) {
	static char errmsg[256] = ">>> ERROR: DInput error ";
	static char *err_ = errmsg+24;
	snprintf (err_, 256-24, "%s (errno %d)", strerror (err), err); // evdev reports errno values, not DIERR codes
	LogOut ("---------------------------------------------------------------");
	LogOut (errmsg);
	sprintf (logs, ">>> [%s | %s | %d]", func, file, line);
	LogOut();
	LogOut ("---------------------------------------------------------------");
}

void LogOut_Warning(const char* func, const char* file, int line, const char* msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	LogOut_WarningVA(func, file, line, msg, ap);
	va_end(ap);
}

void LogOut_Obsolete(const char* func, const char* msg)
{
	LogOut_Obsolete_Start();
	LogOut("Obsolete API function used: %s", func);
	if (msg)
		LogOut(msg);
	else {
		LogOut("At least one active module is accessing an obsolete interface function.");
		LogOut("Addons which rely on obsolete functions may not be compatible with");
		LogOut("future versions of Orbiter.");
	}
	LogOut_Warning_End();
}


void PrintModules()
{
	// EnumProcessModules counterpart: every loaded ELF object; ELF has no VS_VERSIONINFO, so path and mapped size only
	dl_iterate_phdr ([](struct dl_phdr_info *info, size_t, void *) -> int {
		if (!info->dlpi_name || !info->dlpi_name[0]) return 0;
		ElfW(Addr) lo = ~(ElfW(Addr))0, hi = 0;
		for (int i = 0; i < info->dlpi_phnum; i++) {
			const ElfW(Phdr) &ph = info->dlpi_phdr[i];
			if (ph.p_type != PT_LOAD) continue;
			if (ph.p_vaddr < lo) lo = ph.p_vaddr;
			if (ph.p_vaddr + ph.p_memsz > hi) hi = ph.p_vaddr + ph.p_memsz;
		}
		LogOut("Module linked [%s]  Size=%u", info->dlpi_name, (unsigned)(hi > lo ? hi - lo : 0));
		return 0;
	}, NULL);
	return;
}



void tracenew (char *fname, int line)
{
#define TESTALLOC 2
#if TESTALLOC == 1
	ofstream ofs("tracenew.txt", ios::app);
	sprintf (DBG_MSG, "T=%f, %s: %d", SimT, fname, line);
	ofs << DBG_MSG << endl;
	ofs.close();
#elif TESTALLOC == 2
	sprintf (DBG_MSG, "T=%f, %s: %d", td.SimT0, fname, line);
#else
	MessageBeep (-1);
#endif
}


// =======================================================================
// Profiler methods
// =======================================================================

static double prof_sum = 0.0;
static uint32_t prof_count = 0;
static std::chrono::time_point<std::chrono::steady_clock> prof_t0;

void StartProf ()
{
	prof_t0 = std::chrono::steady_clock::now();
}

double EndProf (DWORD *count)
{
	auto t1 = std::chrono::steady_clock::now();
	std::chrono::duration<double> time_delta = t1 - prof_t0;

	double dt = time_delta.count();
	if (dt > 0.0) {
		prof_sum += dt;
		prof_count++;
	}
	if (count) *count = prof_count;
	return prof_sum/(double)prof_count;
}

