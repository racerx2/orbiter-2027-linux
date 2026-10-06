// custom: forms skins; a copy of a skin's folder under a new name, for the user to change

#include "SkinCopy.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <vector>

namespace custom {

namespace {

	bool Inside (const QString &path, const QString &dir)
	{
		return path == dir || path.startsWith (dir + '/');
	}

	// a folder name: ASCII letters, digits, - and _
	QString FolderOf (const QString &name)
	{
		QString base;
		for (QChar ch : name)
			base += ((ch.unicode () < 128 && ch.isLetterOrNumber ()) || ch == '-' || ch == '_' ? ch : QChar ('_'));
		return base.isEmpty () ? QString ("Skin") : base;
	}

	// every Name line says the new name, as ReadCfg keeps the last one
	bool Rename (const QString &cfgPath, const QString &name)
	{
		QFile cfg (cfgPath);
		if (!cfg.open (QIODevice::ReadOnly)) return false;
		QString text = QString::fromUtf8 (cfg.readAll ());
		cfg.close ();
		if (text.startsWith (QChar (0xfeff))) text.remove (0, 1);
		QStringList lines = text.split ('\n');
		bool named = false;
		for (QString &l : lines) {
			const int eq = l.indexOf ('=');
			if (eq > 0 && !l.left (eq).trimmed ().compare ("Name", Qt::CaseInsensitive)) {
				l = "Name = " + name + (l.endsWith ('\r') ? "\r" : "");
				named = true;
			}
		}
		if (!named) lines.prepend ("Name = " + name);
		return cfg.open (QIODevice::WriteOnly | QIODevice::Truncate) && cfg.write (lines.join ('\n').toUtf8 ()) >= 0;
	}

}

bool CopySkin (const SkinManifest &m, const QString &skinsDir, const QString &nameIn, SkinCopy &out)
{
	const QString from = QString::fromStdString (m.name);
	const QString srcDir = (m.dir.empty () ? QString () : QFileInfo (QString::fromStdString (m.dir)).canonicalFilePath ());
	if (srcDir.isEmpty () || !QFileInfo (srcDir).isDir ()) { // QDir ("") would be the working folder
		out.msg = "The folder of '" + from + "' was not found.";
		return false;
	}
	QString nm = nameIn.simplified ().left (COPY_MAX_NAME).trimmed ();
	if (nm.isEmpty ()) nm = ("My " + from).left (COPY_MAX_NAME).trimmed ();

	const QDir src (srcDir);
	struct Link { QString rel, target; };
	QStringList files, dirs, skipped, problems;
	std::vector<Link> links;
	qint64 bytes = 0;
	QDirIterator it (srcDir, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
	while (it.hasNext ()) {
		const QFileInfo fi (it.next ());
		const QString rel = src.relativeFilePath (fi.filePath ());
		if (fi.isSymLink ()) {
			const QString t = fi.canonicalFilePath ();
			if (t.isEmpty () || !Inside (t, srcDir)) skipped << rel; // it would lead out of the copy
			else links.push_back ({rel, QDir (fi.absolutePath ()).relativeFilePath (t)});
		} else if (fi.isDir ()) {
			dirs << rel;
			if (!fi.isReadable () || !fi.isExecutable ()) problems << rel + " can't be read";
		} else if (fi.isFile ()) {
			files << rel;
			bytes += fi.size ();
		} else skipped << rel; // a pipe or a socket
		if (files.size () + dirs.size () + (qsizetype)links.size () > COPY_MAX_FILES || bytes > COPY_MAX_BYTES) {
			out.msg = QString ("'%1' is too big to copy here (more than %2 files or 64 MiB).").arg (from).arg (COPY_MAX_FILES);
			return false;
		}
	}
	if (!problems.isEmpty ()) {
		out.msg = "'" + from + "' can't be copied:\n- " + problems.mid (0, 12).join ("\n- ");
		return false;
	}

	const QDir skins (skinsDir);
	const QString base = FolderOf (nm);
	QString folder = base;
	int k = 1;
	while (!folder.compare ("classic", Qt::CaseInsensitive) || skins.exists (folder)) folder = base + "_" + QString::number (++k);
	const QString display = (k > 1 ? nm + " " + QString::number (k) : nm); // two copies don't share a name

	QTemporaryDir tmp (skins.absoluteFilePath (".copy-" + folder + "-XXXXXX")); // ScanSkins skips dot names; removed on failure
	if (!tmp.isValid ()) {
		out.msg = "The Skins folder can't be written: " + skins.absolutePath () + "\nCheck its permissions, or run Orbiter from a folder you own.";
		return false;
	}
	QFile::setPermissions (tmp.path (), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
		| QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther);
	const QDir dst (tmp.path ());
	for (const QString &d : dirs)
		if (!dst.mkpath (d)) problems << d + " can't be made";
	for (const QString &f : files) {
		const QString to = dst.absoluteFilePath (f);
		if (!QFile::copy (src.absoluteFilePath (f), to)) problems << f + " can't be copied";
		else QFile::setPermissions (to, QFile::permissions (to) | QFileDevice::WriteOwner); // the copy is for editing
	}
	for (const Link &l : links)
		if (!QFile::link (l.target, dst.absoluteFilePath (l.rel))) problems << l.rel + " (a link) can't be made";
	if (problems.isEmpty () && !Rename (dst.absoluteFilePath ("skin.cfg"), display)) problems << "skin.cfg can't be written";
	if (problems.isEmpty () && !QDir ().rename (tmp.path (), skins.absoluteFilePath (folder))) problems << "the folder can't be renamed to " + folder;
	if (!problems.isEmpty ()) {
		out.msg = "'" + from + "' was not copied:\n- " + problems.mid (0, 12).join ("\n- ");
		return false;
	}
	tmp.setAutoRemove (false);
	out.id = folder;
	out.name = display;
	out.msg = QString ("Copied '%1' to '%2' in\n%3").arg (from, display, skins.absoluteFilePath (folder));
	if (!skipped.isEmpty ()) out.msg += "\n\nLeft out (links out of the skin, pipes):\n- " + skipped.mid (0, 12).join ("\n- ");
	return true;
}

}
