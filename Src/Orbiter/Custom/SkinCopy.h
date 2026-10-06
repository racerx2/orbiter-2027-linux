// custom: forms skins; a copy of a skin's folder under a new name, for the user to change

#ifndef __CUSTOM_SKINCOPY_H
#define __CUSTOM_SKINCOPY_H

#include "LauncherFacts.h"
#include <QString>

namespace custom {

	const int COPY_MAX_FILES = 4000;                     // files, folders and links
	const qint64 COPY_MAX_BYTES = 64LL * 1024 * 1024;
	const int COPY_MAX_NAME = 64;

	struct SkinCopy {
		QString id;   // the new folder
		QString name; // its Name in skin.cfg
		QString msg;  // what was done, or what went wrong
	};

	// copies m's folder into skinsDir, all or nothing; links inside the skin stay links, links out of it are left out
	bool CopySkin (const SkinManifest &m, const QString &skinsDir, const QString &name, SkinCopy &out);

}

#endif // !__CUSTOM_SKINCOPY_H
