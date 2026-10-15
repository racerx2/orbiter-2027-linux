// not upstream: private font files (oapiAddFontResource), the Linux counterpart of AddFontResourceEx/RemoveFontResourceEx with FR_PRIVATE

#define OAPI_IMPLEMENTATION
#include "OrbiterAPI.h"
#include "Log.h"
#include <QCoreApplication>
#include <QFontDatabase>
#include <QString>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>

static std::map<std::string, std::pair<int, int>> g_fontRes; // resolved path -> (QFontDatabase id, count)
static bool g_appClosing = false; // Qt is going away: removals do nothing

// set before exit (0), on aboutToQuit and at exit: ExitModule may run from _dl_fini, after Qt's statics
void FontResourceExit ()
{
	g_appClosing = true;
}

// called first in main, before QApplication: the handler runs before _dl_fini
void FontResourceInit ()
{
	std::atexit (FontResourceExit);
}

DLLEXPORT bool oapiAddFontResource (const char *path)
{
	static bool connected = false;
	QCoreApplication *app = QCoreApplication::instance ();
	if (!path || !app || g_appClosing) return false;
	if (!connected) connected = (bool)QObject::connect (app, &QCoreApplication::aboutToQuit, FontResourceExit);
	std::string file = oapiResolvePath (path);
	auto it = g_fontRes.find (file);
	if (it != g_fontRes.end ()) {
		it->second.second++;
		return true;
	}
	int id = QFontDatabase::addApplicationFont (QString::fromStdString (file));
	if (id < 0) {
		LOGOUT_WARN ("Font resource %s could not be added", path);
		return false;
	}
	g_fontRes[file] = std::make_pair (id, 1);
	return true;
}

DLLEXPORT bool oapiRemoveFontResource (const char *path)
{
	if (g_appClosing) return true;
	if (!path) return false;
	auto it = g_fontRes.find (oapiResolvePath (path));
	if (it == g_fontRes.end ()) return false;
	if (!--it->second.second) {
		QFontDatabase::removeApplicationFont (it->second.first);
		g_fontRes.erase (it);
	}
	return true;
}
