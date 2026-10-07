// not upstream: collision addon, the hook of the exported functions of CollisionAPI.h (Design CA E3 6.1, 10); E4 sets it once per process
#ifndef COLLAPIA_H
#define COLLAPIA_H
class CollDmgSession;
namespace CollApiA {
	typedef CollDmgSession *(*SessionFn) ();   // the running session's E3 part, NULL without one
	void SetSession (SessionFn fn);            // InitModule: set; ExitModule: NULL
}
#endif
