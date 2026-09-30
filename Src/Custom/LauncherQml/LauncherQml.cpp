// custom: launcher skins; Modules/Launcher/LauncherQml.so, the QML view of a launcher skin (loaded only for QML skins)

#include "LauncherQmlAbi.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QLibraryInfo>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QQmlAbstractUrlInterceptor>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickItem>
#include <QQuickWidget>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>
#include <QtQml>
#include <algorithm>
#include <cstring>
#include <memory>

namespace {

	void (*g_log) (const char *line) = nullptr;
	QObject *g_api = nullptr;
	bool g_registered = false;
	std::unique_ptr<QTemporaryDir> g_allowDir; // the allow-listed Qt QML modules, as symlinks
	QString g_allowPath;
	QSet<QString> g_realDirs;                  // canonical Qt directories of the allowed modules
	const int MAX_WARNINGS = 100;

	void Log (const QString &s)
	{
		if (g_log) g_log (("Launcher skin (QML): " + s).toUtf8 ().constData ());
	}

	bool Under (const QString &path, const QString &dir)
	{
		return !dir.isEmpty () && (path == dir || path.startsWith (dir + '/'));
	}

	// allowed modules and everything they import or depend on
	bool BuildAllowDir (QString &err)
	{
		if (g_allowDir) return true;
		const QString qt = QLibraryInfo::path (QLibraryInfo::QmlImportsPath);
		QStringList queue = {"QtQml", "QtQml/Models", "QtQml/WorkerScript", "QtQuick", "QtQuick/Window",
			"QtQuick/Layouts", "QtQuick/Shapes", "QtQuick/Effects", "QtQuick/Particles"};
		QStringList allowed;
		while (!queue.isEmpty ()) {
			QString m = queue.takeFirst ();
			if (allowed.contains (m)) continue;
			QFile f (qt + '/' + m + "/qmldir");
			if (!f.open (QIODevice::ReadOnly)) {
				Log ("Qt module " + m + " not installed");
				continue;
			}
			allowed << m;
			QTextStream ts (&f);
			while (!ts.atEnd ()) {
				QStringList w = ts.readLine ().simplified ().split (' ', Qt::SkipEmptyParts);
				if (!w.isEmpty () && (w[0] == "optional" || w[0] == "default")) w.removeFirst ();
				if (w.size () >= 2 && (w[0] == "import" || w[0] == "depends"))
					queue << QString (w[1]).replace ('.', '/');
			}
		}
		QString base = QStandardPaths::writableLocation (QStandardPaths::RuntimeLocation); // per-user tmpfs, emptied at logout
		if (base.isEmpty () || !QFileInfo (base).isWritable ()) base = QDir::tempPath ();
		auto dir = std::make_unique<QTemporaryDir> (base + "/orbiter-qml-XXXXXX");
		if (!dir->isValid ()) {
			err = "can't create the QML module directory: " + dir->errorString ();
			return false;
		}
		for (const QString &m : allowed) {
			QDir src (qt + '/' + m);
			QString dst = dir->path () + '/' + m;
			if (!QDir ().mkpath (dst)) {
				err = "can't create " + dst;
				return false;
			}
			for (const QFileInfo &fi : src.entryInfoList (QDir::Files)) // files only: sub-modules are allowed one by one
				QFile::link (fi.absoluteFilePath (), dst + '/' + fi.fileName ());
			g_realDirs.insert (QFileInfo (src.absolutePath ()).canonicalFilePath ());
		}
		g_allowPath = QDir::cleanPath (dir->path ());
		g_allowDir = std::move (dir);
		Log ("allowed Qt modules: " + allowed.join (", "));
		return true;
	}

	// everything but qrc: and data: fails; the factory may be called from loader threads
	class BlockingNam: public QNetworkAccessManager {
	protected:
		QNetworkReply *createRequest (Operation op, const QNetworkRequest &req, QIODevice *data) override
		{
			QString s = req.url ().scheme ();
			if (s == "qrc" || s == "data") return QNetworkAccessManager::createRequest (op, req, data);
			return QNetworkAccessManager::createRequest (op, QNetworkRequest (QUrl ("blocked:network")), data);
		}
	};

	class NamFactory: public QQmlNetworkAccessManagerFactory {
	public:
		QNetworkAccessManager *create (QObject *parent) override
		{
			auto *nam = new BlockingNam;
			nam->setParent (parent);
			return nam;
		}
	};

	NamFactory g_namFactory;

	// file: URLs only inside the skin folder or the allowed modules; qrc: and data: pass
	class Interceptor: public QQmlAbstractUrlInterceptor {
	public:
		QString skinDir; // canonical
		QUrl intercept (const QUrl &url, DataType) override
		{
			QString s = url.scheme ();
			if (s == "qrc" || s == "data") return url;
			if (s != "file") return QUrl ();
			QString p = QDir::cleanPath (url.toLocalFile ());
			if (Under (p, g_allowPath)) return url;
			if (Under (p, skinDir)) {
				QString c = QFileInfo (p).canonicalFilePath ();
				if (c.isEmpty () || Under (c, skinDir)) return url; // a missing file is harmless
			}
			QFileInfo fi (p);
			if (g_realDirs.contains (QFileInfo (fi.absolutePath ()).canonicalFilePath ())) return url;
			return QUrl ();
		}
	};

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

extern "C" Q_DECL_EXPORT QWidget *LauncherQml_Create (const LauncherQmlInit *init, char *err, int errlen)
{
	if (!init || init->abi != LAUNCHERQML_ABI || !init->qtVersion || strcmp (init->qtVersion, QT_VERSION_STR)) {
		CopyErr (err, errlen, QString ("LauncherQml.so was built for another Orbiter or Qt (Qt %1)").arg (QT_VERSION_STR));
		return nullptr;
	}
	g_log = init->log;
	if (!g_registered) {
		g_api = init->api;
		// a singleton type is created per engine: every engine of the run gets the one API object
		qmlRegisterSingletonType<QObject> ("Orbiter.Launcher", 1, 0, "Launcher", [](QQmlEngine*, QJSEngine*) -> QObject* {
			QJSEngine::setObjectOwnership (g_api, QJSEngine::CppOwnership);
			return g_api;
		});
		g_registered = true;
	}
	QString e;
	if (!BuildAllowDir (e)) {
		CopyErr (err, errlen, e);
		return nullptr;
	}

	const QString skinDir = QFileInfo (QString::fromUtf8 (init->skinDir)).canonicalFilePath ();
	auto *engine = new QQmlEngine;
	engine->setImportPathList ({skinDir, g_allowPath});
	engine->setNetworkAccessManagerFactory (&g_namFactory);
	auto *icpt = new Interceptor;
	icpt->skinDir = skinDir;
	engine->addUrlInterceptor (icpt);
	QObject::connect (engine, &QObject::destroyed, [icpt]() { delete icpt; });
	auto warnings = std::make_shared<int> (0);
	QObject::connect (engine, &QQmlEngine::warnings, engine, [warnings](const QList<QQmlError> &list) {
		for (const QQmlError &w : list) {
			if (*warnings > MAX_WARNINGS) return;
			if (++*warnings > MAX_WARNINGS) Log ("further warnings suppressed");
			else Log (w.toString ());
		}
	});
	QObject::connect (engine, &QQmlEngine::quit, engine, []() { Log ("Qt.quit() ignored: use Launcher.quit()"); });

	auto *view = new QQuickWidget (engine, init->parent);
	engine->setParent (view); // deleted after the view: ~QWidget deletes it once ~QQuickWidget is done
	view->hide ();
	view->setResizeMode (QQuickWidget::SizeRootObjectToView);
	view->setFocusPolicy (Qt::StrongFocus);
	view->setSource (QUrl::fromLocalFile (skinDir + '/' + QString::fromUtf8 (init->entry)));
	if (view->status () != QQuickWidget::Ready) {
		QStringList msgs;
		for (const QQmlError &x : view->errors ()) {
			Log (x.toString ());
			if (msgs.size () < 5) msgs << x.toString ();
		}
		if (msgs.isEmpty ()) msgs << "the QML entry file did not load";
		CopyErr (err, errlen, msgs.join ('\n'));
		delete view;
		return nullptr;
	}
	Log ("view created for " + skinDir);
	return view;
}

extern "C" Q_DECL_EXPORT void LauncherQml_Focus (QWidget *view)
{
	QQuickWidget *w = qobject_cast<QQuickWidget*> (view);
	QQuickItem *root = (w ? w->rootObject () : nullptr);
	if (root && !root->hasActiveFocus ()) root->forceActiveFocus (); // the focus inside the root's scope stays where it was
}

extern "C" Q_DECL_EXPORT void LauncherQml_Destroy (QWidget *view)
{
	if (!view) return;
	delete view;
	Log ("view destroyed");
}
