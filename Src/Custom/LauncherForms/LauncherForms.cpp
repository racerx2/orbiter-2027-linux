
// custom: forms skins; Modules/Launcher/LauncherForms.so, the Qt Designer form view of a launcher skin (loaded only for forms skins)

#include "LauncherQmlAbi.h"
#include "FormsView.h"
#include <QFileInfo>
#include <algorithm>
#include <cstring>

namespace {

	void (*g_log) (const char *line) = nullptr;

	void Log (const QString &s)
	{
		if (g_log) g_log (("Launcher skin (forms): " + s).toUtf8 ().constData ());
	}

	void CopyErr (char *err, int errlen, const QString &s)
	{
		if (!err || errlen <= 0) return;
		QByteArray b = s.toUtf8 ();
		int n = std::min ((int)b.size (), errlen - 1);
		while (n > 0 && n < (int)b.size () && ((unsigned char)b[n] & 0xC0) == 0x80) n--; // not inside a UTF-8 character
		memcpy (err, b.constData (), n);
		err[n] = '\0';
	}

}

extern "C" Q_DECL_EXPORT QWidget *LauncherForms_Create (const LauncherQmlInit *init, char *err, int errlen)
{
	if (!init || init->abi != LAUNCHERQML_ABI || !init->qtVersion || strcmp (init->qtVersion, QT_VERSION_STR)) {
		CopyErr (err, errlen, QString ("LauncherForms.so was built for another Orbiter or Qt (Qt %1)").arg (QT_VERSION_STR));
		return nullptr;
	}
	g_log = init->log;
	const QString skinDir = QFileInfo (QString::fromUtf8 (init->skinDir)).canonicalFilePath ();
	auto *view = new forms::FormsView (init->parent, init->api, skinDir, QString::fromUtf8 (init->entry), Log);
	view->hide ();
	QString e;
	if (!view->Load (e)) {
		Log (e);
		CopyErr (err, errlen, e);
		delete view;
		return nullptr;
	}
	Log ("view created for " + skinDir);
	return view;
}

extern "C" Q_DECL_EXPORT void LauncherForms_Focus (QWidget *view)
{
	if (auto *v = qobject_cast<forms::FormsView*> (view)) v->FocusView ();
}

extern "C" Q_DECL_EXPORT void LauncherForms_Destroy (QWidget *view)
{
	if (!view) return;
	delete view;
	Log ("view destroyed");
}
