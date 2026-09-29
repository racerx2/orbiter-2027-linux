// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#include "console_ng.h"
#include "Orbiter.h"
#include "DlgMgr.h"
#include "Psys.h"
#include "Vessel.h"
#include "Log.h"
#include "DlgFocus.h"
#include "DlgMap.h"
#include "DlgInfo.h"
#include "DlgTacc.h"
#include "DlgFunction.h"
#include "DlgRecorder.h"
#include "DlgHelp.h"
#include "ConsoleManager.h"
#include "resource.h"
#include "ResDialog.h"
#include <QKeyEvent>
#include <QTimer>
#include <mutex>
#include <poll.h>
#include <unistd.h>
#include <errno.h>

extern PlanetarySystem* g_psys;
extern Vessel* g_focusobj;
extern TimeData td;

static void InputProc(std::atomic<bool>* stop);
static void ConsoleLine(const char* line);
static void ServerDlgProc(QWidget* hDlg, void* context);
static void ConsoleOut(const char* msg);

static std::mutex hMutex;
static FILE* s_hStdO = NULL;
static char cConsoleCmd[1024] = "\0";
static orbiter::ConsoleNG* s_console = NULL; // access to console instance from message callback functions

orbiter::ConsoleNG::ConsoleNG(Orbiter* pOrbiter)
    : m_pOrbiter(pOrbiter)
    , m_hWnd(NULL)
    , m_hStatWnd(NULL)
    , m_stop(false)
{
    static PCSTR title = "Orbiter Server Console";

    s_console = this;

    ConsoleManager::ShowConsole(true);
    if (isatty(STDOUT_FILENO)) {
        printf("\033]0;%s\007", title); // SetConsoleTitle
        fflush(stdout);
    }
    ConsoleManager::SetConsoleTitle(title);
    m_hWnd = ConsoleManager::ConsoleWindow(); // NULL on a terminal; Orbiter's console window has no close button
    ConsoleManager::SetConsoleInput(&ConsoleLine);
    m_thread = std::thread(InputProc, &m_stop);
    s_hStdO = stdout;
    SetLogOutFunc(&ConsoleOut); // clone log output to console
}

orbiter::ConsoleNG::~ConsoleNG()
{
	DestroyStatDlg();
	SetLogOutFunc(0);
	ConsoleManager::SetConsoleInput(NULL);
	m_stop = true; // the thread polls stdin and exits within 200 ms
	if (m_thread.joinable())
		m_thread.join();
	if (isatty(STDOUT_FILENO)) {
		fputs("\033[0m", stdout); // leave the terminal in its default colours
		fflush(stdout);
	}
	s_console = NULL;
	s_hStdO = NULL;
}

bool orbiter::ConsoleNG::ParseCmd()
{
	if (!cConsoleCmd[0]) return false;
	char cmd[1024], cbuf[256], * pc, * ppc;

	{
		std::lock_guard<std::mutex> lock(hMutex);
		strcpy(cmd, cConsoleCmd + 1);
		cConsoleCmd[0] = '\0';
	}

	DWORD i;
	if (!strncasecmp(cmd, "help", 4)) {
		pc = trim_string(cmd + 4);
		if (!strncasecmp(pc, "help", 4)) {
			Echo("Brief onscreen help for console commands.");
			Echo("Type \"help\" followed by a top-level command to get information for this command.");
		}
		else if (!strncasecmp(pc, "exit", 4)) {
			Echo("Exits the simulation session and returns to the Launchpad dialog.");
		}
		else if (!strncasecmp(pc, "vessel", 6)) {
			ppc = trim_string(pc + 6);
			if (!strncasecmp(ppc, "list", 4)) {
				Echo("Lists all vessels in the current session.");
			}
			else if (!strncasecmp(ppc, "count", 5)) {
				Echo("Prints the number of vessels in the current session.");
			}
			else if (!strncasecmp(ppc, "focus", 5)) {
				Echo("Prints the name of the current focus vessel.");
			}
			else if (!strncasecmp(ppc, "del", 3)) {
				Echo("vessel del <name> -- Destroy vessel <name>.");
			}
			else {
				Echo("Vessel-specific commands. The following sub-commands are recognized:\n");
				Echo("list count focus del\n");
				Echo("Type \"help vessel <subcommand>\" to get information for a command.");
			}
		}
		else if (!strncasecmp(pc, "time", 4)) {
			Echo("Output current simulation time.");
			Echo("time syst  --  Session up time (seconds)");
			Echo("time simt  --  Simulation time (seconds)");
			Echo("time mjd   --  Absolute simulation time (MJD format)");
			Echo("time ut    --  Absolute simulation time (UT format)");
			Echo("Without arguments, all 4 time values are displayed.");
		}
		else if (!strncasecmp(pc, "tacc", 4)) {
			Echo("Display or set time acceleration factor.");
			Echo("tacc <x>  --  Set new time acceleration factor x.");
			Echo("Without argument, prints the current time acceleration factor.");
		}
		else if (!strncasecmp(pc, "pause", 5)) {
			Echo("Pause/resume simulation session.");
			Echo("pause on      --  pause simulation");
			Echo("pause off     --  resume simulation");
			Echo("pause toggle  --  toggle pause/resume state");
			Echo("Without arguments, the current simulation state is displayed.");
		}
		else if (!strncasecmp(pc, "step", 4)) {
			Echo("Display momentary simulation step length and steps per second.");
		}
		else if (!strncasecmp(pc, "dlg", 3)) {
			Echo("Open a dialog.");
			Echo("dlg focus    -- Open the vessel selction dialog");
			Echo("dlg map      -- Open the map window");
			Echo("dlg info     -- Open the object info dialog");
			Echo("dlg tacc     -- Open the time acceleration dialog");
			Echo("dlg help     -- Open the help dialog");
			Echo("dlg record   -- Open the flight recorder dialog");
			Echo("dlg function -- Open the plugin function list");
		}
		else if (!strncasecmp(pc, "gui", 3)) {
			Echo("Toggles the display of a dialog box that continuously monitors the simulation");
			Echo("state.");
		}
		else {
			Echo("The following top-level commands are available:\n");
			Echo("  help exit vessel time tacc pause step dlg gui\n");
			Echo("To get help for a command, type \"help <cmd>\"");
		}
	}
	else if (!strncasecmp(cmd, "exit", 4)) {
		m_pOrbiter->CloseSession();
		return true;
	}
	else if (!strncasecmp(cmd, "vessel", 6)) {
		pc = trim_string(cmd + 6);
		if (!strncasecmp(pc, "list", 4)) {
			for (i = 0; i < g_psys->nVessel(); i++)
				Echo(g_psys->GetVessel(i)->Name());
			return true;
		}
		else if (!strncasecmp(pc, "count", 5)) {
			sprintf(cbuf, "%zu", g_psys->nVessel());
			Echo(cbuf);
		}
		else if (!strncasecmp(pc, "focus", 5)) {
			Echo(g_focusobj->Name());
		}
		else if (!strncasecmp(pc, "del", 3)) {
			Vessel* v = g_psys->GetVessel(trim_string(pc + 3), true);
			if (v) v->RequestDestruct();
		}
	}
	else if (!strncasecmp(cmd, "tacc", 4)) {
		double w;
		if (sscanf(trim_string(cmd + 4), "%lf", &w) == 1)
			m_pOrbiter->SetWarpFactor(w);
		else {
			sprintf(cbuf, "Time acceleration is %0.1f", td.Warp());
			Echo(cbuf);
		}
	}
	else if (!strncasecmp(cmd, "time", 4)) {
		pc = trim_string(cmd + 4);
		if (!strncasecmp(pc, "simt", 4)) {
			sprintf(cbuf, "%0.1f", td.SimT0);
		}
		else if (!strncasecmp(pc, "syst", 4)) {
			sprintf(cbuf, "%0.1f", td.SysT0);
		}
		else if (!strncasecmp(pc, "mjd", 3)) {
			sprintf(cbuf, "%0.6f", td.MJD0);
		}
		else if (!strncasecmp(pc, "ut", 2)) {
			strcpy(cbuf, DateStr(td.MJD0));
		}
		else {
			sprintf(cbuf, "SysT=%0.1f SimT=%0.1f, MJD=%0.6f, UT=%s", td.SysT0, td.SimT0, td.MJD0, DateStr(td.MJD0));
		}
		Echo(cbuf);
	}
	else if (!strncasecmp(cmd, "pause", 5)) {
		pc = trim_string(cmd + 5);
		if (!strncasecmp(pc, "on", 2)) m_pOrbiter->Pause(true);
		else if (!strncasecmp(pc, "off", 3)) m_pOrbiter->Pause(false);
		else if (!strncasecmp(pc, "toggle", 6)) m_pOrbiter->TogglePause();
		snprintf(cbuf, 256, "Simulation %s", m_pOrbiter->IsRunning() ? "running" : "paused");
		Echo(cbuf);
	}
	else if (!strncasecmp(cmd, "step", 4)) {
		snprintf(cbuf, 256, "dt=%f, FPS=%f", td.SimDT, td.FPS());
		Echo(cbuf);
	}
	else if (!strncasecmp(cmd, "gui", 3)) {
		if (!DestroyStatDlg()) {
			m_hStatWnd = oapiCreateResDialog(m_pOrbiter->GetInstance(), IDD_SERVER, NULL, m_hWnd);
			if (m_hStatWnd)
				ServerDlgProc(m_hStatWnd, this);
		}
	}
	else if (!strncasecmp(cmd, "dlg", 3)) {
		DialogManager* pDlgMgr = m_pOrbiter->DlgMgr();
		if (pDlgMgr) {
			pc = trim_string(cmd + 3);
			if (!strncasecmp(pc, "focus", 5))
				pDlgMgr->EnsureEntry<DlgFocus>();
			else if (!strncasecmp(pc, "map", 3))
				pDlgMgr->EnsureEntry<DlgMap>();
			else if (!strncasecmp(pc, "info", 4))
				pDlgMgr->EnsureEntry<DlgInfo>();
			else if (!strncasecmp(pc, "tacc", 4))
				pDlgMgr->EnsureEntry<DlgTacc>();
			else if (!strncasecmp(pc, "function", 8))
				pDlgMgr->EnsureEntry<DlgFunction>();
			else if (!strncasecmp(pc, "record", 6))
				pDlgMgr->EnsureEntry<DlgRecorder>();
			else if (!strncasecmp(pc, "help", 4))
				pDlgMgr->EnsureEntry<DlgHelp>();
		}
	}
	return false;
}

void orbiter::ConsoleNG::Echo(const char* str) const
{
	ConsoleOut(str);
}

void orbiter::ConsoleNG::EchoIntro() const
{
	Echo("-----------------\nOrbiter NG (no graphics)");
	Echo("Running in server mode (no graphics client attached).");
	Echo("Type \"help\" for a list of commands.");
	Echo("Type \"exit\" to return to the Launchpad dialog.\n");
}

bool orbiter::ConsoleNG::DestroyStatDlg()
{
	if (m_hStatWnd) {
		m_hStatWnd->deleteLater();
		m_hStatWnd = NULL;
		return true;
	}
	else
		return false;
}


// SetConsoleTextAttribute: ANSI white, bright for intensity, only on a terminal
static void ConsoleColour(FILE* f, bool intensity)
{
	if (isatty(fileno(f)))
		fputs(intensity ? "\033[1;37m" : "\033[0;37m", f);
}

void InputProc(std::atomic<bool>* stop)
{
	ssize_t count;
	char cbuf[1024];
	// ReadConsole line input and echo are the terminal's canonical mode
	for (;;) {
		pollfd pfd = { STDIN_FILENO, POLLIN, 0 };
		int res = poll(&pfd, 1, 200);
		if (*stop) break;
		if (res == 0) continue;
		if (res < 0 && errno == EINTR) continue;
		if (res < 0 || (count = read(STDIN_FILENO, cbuf, sizeof(cbuf) - 2)) <= 0)
			break; // Console not available, exiting
		fputs("> ", stdout);
		fflush(stdout);

		{
			std::lock_guard<std::mutex> lock(hMutex);
			cConsoleCmd[0] = 'x';
			strncpy(cConsoleCmd + 1, cbuf, count);
			cConsoleCmd[count + 1] = '\0';
			cConsoleCmd[strcspn(cConsoleCmd, "\r\n")] = '\0'; // eliminates CR/LF
		}

		// handle "exit" directly so we can terminate the console thread in an orderly fashion
		if (!strncmp(cbuf, "exit\n", 5) || !strncmp(cbuf, "exit\r\n", 6)) {
			break;
		}
	}
}

// ReadConsole from Orbiter's console window: one typed line, on the GUI thread
void ConsoleLine(const char* line)
{
	std::lock_guard<std::mutex> lock(hMutex);
	cConsoleCmd[0] = 'x';
	snprintf(cConsoleCmd + 1, sizeof(cConsoleCmd) - 1, "%s", line);
}

// WM_TIMER, WM_COMMAND and WM_CLOSE become a QTimer, the command handler and a close event hook
void ServerDlgProc(QWidget* hDlg, void* context)
{
	QTimer* timer = new QTimer(hDlg); // SetTimer; destroyed with the dialog (KillTimer)
	QObject::connect(timer, &QTimer::timeout, hDlg, [hDlg]() {
		if (s_console)
			s_console->GetOrbiter()->UpdateServerWnd(hDlg);
	});
	timer->start(1000);
	oapiConnectDlgCommands(hDlg, [](int id, int code, QWidget* hCtrl) {
		switch (id) {
		case IDOK:
			if (s_console)
				s_console->GetOrbiter()->CloseSession();
		}
	});
	new EventHook(hDlg, [](QObject*, QEvent* event) {
		if (event->type() == QEvent::Close) {
			if (s_console)
				s_console->DestroyStatDlg();
			event->ignore();
			return true;
		}
		if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape)
			return true; // IDCANCEL is not handled
		return false;
	});
}

void ConsoleOut(const char* msg)
{
	if (!s_hStdO) return;
	if (ConsoleManager::WriteConsole(msg)) // Orbiter's console window
		return;
	ConsoleColour(s_hStdO, false);
	fputc('\r', s_hStdO); // cursor to column 0
	fputs(msg, s_hStdO);
	ConsoleColour(s_hStdO, true);
	fputs("\n> ", s_hStdO);
	fflush(s_hStdO);
}
