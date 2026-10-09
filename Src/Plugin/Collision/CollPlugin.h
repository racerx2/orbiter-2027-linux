// not upstream: collision addon, the module object: session lifecycle, callback dispatch, custom command and dialog (Design CA E4 11)
#ifndef COLLPLUGIN_H
#define COLLPLUGIN_H
#include <cstdint>
#include <memory>
#include "CollPlatform.h"
class CollSession;
class CollSdk;
class CollDialogA;

class CollPlugin : public oapi::Module {
public:
	explicit CollPlugin (CollHModule h);   // InitModule: the process CollSdk, the custom command and the dialog object, once per process
	~CollPlugin () override;
	void Shutdown ();                      // ExitModule: a live session ends, then the command and the dialog go
	void LoadState (FILEHANDLE scn);
	void SaveState (FILEHANDLE scn);
	void clbkSimulationStart (RenderMode mode) override;
	void clbkSimulationEnd () override;
	void clbkPreStep (double simt, double simdt, double mjd) override;
	void clbkPostStep (double simt, double simdt, double mjd) override;
	void clbkTimeJump (double simt, double simdt, double mjd) override;
	void clbkTimeAccChanged (double newWarp, double oldWarp) override;
	void clbkNewVessel (OBJHANDLE h) override;
	void clbkDeleteVessel (OBJHANDLE h) override;
	void clbkVesselJump (OBJHANDLE h) override;
	void clbkPause (bool pause) override;
	bool clbkProcessKeyboardImmediate (char kstate[256], bool simRunning) override;
	CollSession *Session () const;         // the running session, NULL otherwise; the command and the dialog ask per call
	static void OnCustomCmd (void *ctx);
private:
	enum class Phase : uint8_t { None, Loading, Running };
	struct Jump { bool pending = false; double simt = 0, simdt = 0, mjd = 0; };
	CollSession *Want (bool startsSession, const char *why);
	void End (const char *why);
	void TimeJump (double simt, double simdt, double mjd);
	std::unique_ptr<CollSdk> proc;         // the process instance: command, dialog, log (W1)
	std::unique_ptr<CollSession> s;
	Phase phase = Phase::None;
	uint32_t serial = 0;
	bool inStep = false;
	bool loaded = false;                   // opcLoadState ran since the last End
	Jump jump;
	int cmd = 0;                           // per process (W1); 0 = none
	CollDialogA *dlg = nullptr;            // per process, holds no session data
};
#endif
