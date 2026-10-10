// custom: launcher skins; the QML view module (LauncherQml.cpp) and the Launcher object a QML skin sees, offscreen

#include <catch2/catch_test_macros.hpp>
#include "LauncherQmlAbi.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWidget>
#include <QTemporaryDir>
#include <memory>

extern "C" QWidget *LauncherQml_Create (const LauncherQmlInit *init, char *err, int errlen);
extern "C" void LauncherQml_Destroy (QWidget *view);

// a Launcher object with a property and a method
class Api: public QObject {
	Q_OBJECT
	Q_PROPERTY(QString version READ version CONSTANT)
public:
	explicit Api (const QString &v): v (v) {}
	QString version () const { return v; }
	Q_INVOKABLE QString ping () const { return "pong " + v; }
private:
	QString v;
};

namespace {

	QApplication &App ()
	{
		static int argc = 1;
		static char arg0[] = "LauncherQml.Test";
		static char *argv[] = {arg0, nullptr};
		if (qEnvironmentVariableIsEmpty ("QT_QPA_PLATFORM")) qputenv ("QT_QPA_PLATFORM", "offscreen");
		if (qEnvironmentVariableIsEmpty ("QT_QUICK_BACKEND")) qputenv ("QT_QUICK_BACKEND", "software");
		static QApplication app (argc, argv);
		return app;
	}

	QStringList g_lines;
	void Log (const char *line) { g_lines << QString::fromUtf8 (line); }

	// a skin that tries to get rid of the Launcher object and then uses it
	const char *MAIN_QML = R"(import QtQuick
import Orbiter.Launcher 1.0
Item {
	property string version: Launcher.version
	property string deleteLaterType: typeof Launcher.deleteLater
	property bool deleteLaterThrows: false
	property bool destroyThrows: false
	property string after: ""
	Component.onCompleted: {
		try { Launcher.deleteLater () } catch (e) { deleteLaterThrows = true }
		try { Launcher.destroy () } catch (e) { destroyThrows = true }
		after = Launcher.ping ()
	}
}
)";

	struct Skin {
		QTemporaryDir tmp;
		QString dir;
		Skin ()
		{
			App ();
			REQUIRE (tmp.isValid ());
			dir = QDir (tmp.path ()).canonicalPath ();
			QFile f (dir + "/Main.qml");
			REQUIRE (f.open (QIODevice::WriteOnly));
			f.write (MAIN_QML);
		}
	};

	struct View {
		QWidget host;
		QWidget *view = nullptr;
		QString err;
		View (const Skin &s, QObject *api)
		{
			const QByteArray dir = s.dir.toUtf8 ();
			LauncherQmlInit init {LAUNCHERQML_ABI, QT_VERSION_STR, &host, api, dir.constData (), "Main.qml", Log};
			char buf[2048] = "";
			view = LauncherQml_Create (&init, buf, sizeof (buf));
			err = QString::fromUtf8 (buf);
		}
		~View () { if (view) LauncherQml_Destroy (view); }
		QQuickItem *Root () const { return view ? static_cast<QQuickWidget*> (view)->rootObject () : nullptr; }
		QVariant Prop (const char *name) const { return Root () ? Root ()->property (name) : QVariant (); }
	};

}

TEST_CASE ("QML: the Launcher object has no deleteLater and can't be destroyed from a skin")
{
	Skin s;
	auto api = std::make_unique<Api> ("A");
	QPointer<Api> alive (api.get ());
	View v (s, api.get ());
	INFO (v.err.toStdString () + " | " + g_lines.join (" | ").toStdString ());
	REQUIRE (v.view);
	REQUIRE (v.Root ());
	CHECK (v.Prop ("version").toString () == "A");
	CHECK (v.Prop ("deleteLaterType").toString () == "undefined");
	CHECK (v.Prop ("deleteLaterThrows").toBool ());
	CHECK (v.Prop ("destroyThrows").toBool ());
	CHECK (v.Prop ("after").toString () == "pong A");
	QCoreApplication::sendPostedEvents (nullptr, QEvent::DeferredDelete);
	QCoreApplication::processEvents ();
	CHECK (alive);
}

TEST_CASE ("QML: every view gets the Launcher object of its own Create")
{
	Skin s;
	{
		auto a = std::make_unique<Api> ("first");
		View v (s, a.get ());
		REQUIRE (v.view);
		CHECK (v.Prop ("version").toString () == "first");
	}
	auto b = std::make_unique<Api> ("second"); // the host made a new one; the first is gone
	View w (s, b.get ());
	INFO (w.err.toStdString ());
	REQUIRE (w.view);
	CHECK (w.Prop ("version").toString () == "second");
	CHECK (w.Prop ("after").toString () == "pong second");
}

#include "LauncherQml.Test.moc"
