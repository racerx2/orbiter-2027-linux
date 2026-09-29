// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#ifndef __console_ng_h
#define __console_ng_h

#include "OrbiterPlatform.h"
#include <atomic>
#include <thread>

class Orbiter;

namespace orbiter {

	class ConsoleNG {
	public:
		ConsoleNG(Orbiter* pOrbiter);
		~ConsoleNG();

		Orbiter* GetOrbiter() const { return m_pOrbiter; }
		QWindow *WindowHandle() const { return m_hWnd; }
		bool ParseCmd();
		void Echo(const char* str) const;
		void EchoIntro() const;
		bool DestroyStatDlg();

	private:

		Orbiter* m_pOrbiter;
		QWindow *m_hWnd;   // console window handle (the launching terminal has none)
		QWidget *m_hStatWnd; // stats dialog
		std::thread m_thread; // console thread
		std::atomic<bool> m_stop; // asks the console thread to exit
	};

}

#endif // !__console_ng_h
