
// custom: forms skins; files and style sheets of a skin folder: url() and ${SKIN} inside the skin only (Qt only)

#ifndef __CUSTOM_SKINSTYLE_H
#define __CUSTOM_SKINSTYLE_H

#include <QSet>
#include <QString>
#include <functional>
#include <map>

namespace custom {

	struct BuildEnv {
		QString formDir;                  // canonical folder of the form
		QString skinDir;                  // canonical skin folder
		QString rootDir;                  // custom-fix L3: set for a skin's Qss file, relative names are tried here first (Orbiter's folder)
		std::map<QString, QString> qrc;   // ":/prefix/file" -> canonical file path
		std::function<void (const QString &)> warn;
		mutable QSet<QString> warned;     // pictures warned about once
		void Warn (const QString &s) const { if (warn) warn (s); }
	};

	QString ResolveFile (const BuildEnv &env, const QString &name);      // canonical path inside the skin, "" if not
	QString RewriteStyle (const BuildEnv &env, const QString &qss);       // url() and ${SKIN} to absolute paths

	// a skin's Qss file as the Launchpad gets it: a forms skin's through RewriteStyle, others with ${SKIN} replaced
	QString SkinStyleSheet (const QString &skinDir, const QString &qss, bool forms, const std::function<void (const QString &)> &warn);

}

#endif // !__CUSTOM_SKINSTYLE_H
