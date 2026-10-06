// custom: launcher skins; C interface between Orbiter and Modules/Launcher/LauncherQml.so (and LauncherForms.so)

#ifndef __CUSTOM_LAUNCHERQMLABI_H
#define __CUSTOM_LAUNCHERQMLABI_H

#include <QtGlobal>

class QObject;
class QWidget;

#define LAUNCHERQML_ABI 1

struct LauncherQmlInit {
	int abi;                          // LAUNCHERQML_ABI
	const char *qtVersion;            // QT_VERSION_STR the caller was built with
	QWidget *parent;                  // the Launchpad dialog
	QObject *api;                     // the Launcher API object, lives until the teardown
	const char *skinDir;              // canonical absolute path
	const char *entry;                // QML file relative to skinDir
	void (*log) (const char *line);   // one line to Orbiter.log
};

typedef QWidget *(*LauncherQmlCreateFn) (const LauncherQmlInit *init, char *err, int errlen);
typedef void (*LauncherQmlDestroyFn) (QWidget *view);
typedef void (*LauncherQmlFocusFn) (QWidget *view); // gives the skin's root item the active focus (keys reach the skin)

#define LAUNCHERQML_CREATE "LauncherQml_Create"
#define LAUNCHERQML_DESTROY "LauncherQml_Destroy"
#define LAUNCHERQML_FOCUS "LauncherQml_Focus"

// custom: forms skins; Modules/Launcher/LauncherForms.so takes the same init (entry = the form) and functions
#define LAUNCHERFORMS_CREATE "LauncherForms_Create"
#define LAUNCHERFORMS_DESTROY "LauncherForms_Destroy"
#define LAUNCHERFORMS_FOCUS "LauncherForms_Focus"

#endif // !__CUSTOM_LAUNCHERQMLABI_H
