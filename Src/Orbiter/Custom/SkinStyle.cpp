
// custom: forms skins; files and style sheets of a skin folder: url() and ${SKIN} inside the skin only (Qt only)

#include "SkinStyle.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace custom {

namespace {

	bool Inside (const QString &path, const QString &dir)
	{
		return !dir.isEmpty () && (path == dir || path.startsWith (dir + '/'));
	}

}

QString ResolveFile (const BuildEnv &env, const QString &nameIn)
{
	QString name = nameIn.trimmed ();
	if (name.isEmpty ()) return QString ();
	QString path;
	if (name.startsWith ("qrc:")) name = ":" + name.mid (4); // qrc:/a.png is :/a.png
	if (name.startsWith (":/")) {
		auto it = env.qrc.find (QDir::cleanPath (name));
		if (it == env.qrc.end ()) {
			if (!env.warned.contains (name)) { env.warned.insert (name); env.Warn (name + " is not in the form's qrc files"); }
			return QString ();
		}
		return it->second;
	}
	if (name.startsWith ("${SKIN}")) path = env.skinDir + name.mid (7);
	else if (QDir::isAbsolutePath (name)) path = name;
	else path = QDir (env.formDir).absoluteFilePath (name);
	const QString c = QFileInfo (path).canonicalFilePath ();
	if (c.isEmpty () || !Inside (c, env.skinDir) || !QFileInfo (c).isFile ()) {
		if (!env.warned.contains (name)) { env.warned.insert (name); env.Warn (name + ": not a file inside the skin folder"); }
		return QString ();
	}
	return c;
}

QString RewriteStyle (const BuildEnv &env, const QString &qssIn)
{
	QString qss = qssIn;
	if (qss.contains ('\\')) { // escapes could spell url( so that nothing below sees it
		if (!env.warned.contains ("\\")) { env.warned.insert ("\\"); env.Warn ("style sheets: backslashes are removed"); }
		qss.remove ('\\');
	}
	static const QRegularExpression url (R"(url\s*\()", QRegularExpression::CaseInsensitiveOption);
	QString out;
	qsizetype last = 0;
	for (auto m = url.match (qss); m.hasMatch (); m = url.match (qss, last)) {
		out += qss.mid (last, m.capturedStart () - last);
		qsizetype i = m.capturedEnd ();
		while (i < qss.size () && qss[i].isSpace ()) i++;
		QString name;
		if (i < qss.size () && (qss[i] == '"' || qss[i] == '\'')) {
			const QChar q = qss[i++];
			const qsizetype e = qss.indexOf (q, i);
			name = qss.mid (i, (e < 0 ? qss.size () : e) - i);
			i = (e < 0 ? qss.size () : e + 1);
			while (i < qss.size () && qss[i].isSpace ()) i++;
		} else {
			const qsizetype e = qss.indexOf (')', i);
			name = qss.mid (i, (e < 0 ? qss.size () : e) - i).trimmed ();
			i = (e < 0 ? qss.size () : e);
		}
		if (i < qss.size () && qss[i] == ')') i++;
		const QString path = ResolveFile (env, name);
		QString esc = path;
		esc.replace ("\"", "\\\"");
		out += "url(\"" + esc + "\")"; // a file outside the skin becomes url("")
		last = i;
	}
	out += qss.mid (last);
	static const QRegularExpression qprop (R"((?i:qproperty)-(text|toolTip|whatsThis|statusTip|styleSheet|openExternalLinks|textFormat|html|plainText|markdown|source)\b)");
	if (out.contains ("qproperty", Qt::CaseInsensitive)) out.replace (qprop, "blocked-\\1"); // they would skip the checks of SafeValue
	if (out.contains ("${SKIN}")) {
		QString d = env.skinDir;
		d.replace ("\"", "\\\"");
		out.replace ("${SKIN}", d);
	}
	return out;
}

QString SkinStyleSheet (const QString &skinDir, const QString &qss, bool forms, const std::function<void (const QString &)> &warn)
{
	if (!forms) {
		QString dir = skinDir, text = qss;
		dir.replace ("\\", "\\\\");
		dir.replace ("\"", "\\\"");
		text.replace ("${SKIN}", dir);
		return text;
	}
	BuildEnv env; // a forms skin's style sheet reaches its form too: the checks of the form's own style sheets
	env.skinDir = env.formDir = QFileInfo (skinDir).canonicalFilePath ();
	env.warn = warn;
	return RewriteStyle (env, qss);
}

}
