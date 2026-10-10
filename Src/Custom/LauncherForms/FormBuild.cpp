
// custom: forms skins; from the .ui tree to widgets: classes, properties, layouts, pictures, anchors

#include "FormBuild.h"
#include "FormPainted.h"
#include "FormWidgets.h"
#include <QBoxLayout>
#include <QCursor>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMetaEnum>
#include <QMetaProperty>
#include <QProgressBar>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTabWidget>
#include <QUrl>
#include <QXmlStreamReader>
#include <algorithm>

namespace forms {

namespace {

	bool Inside (const QString &path, const QString &dir)
	{
		return !dir.isEmpty () && (path == dir || path.startsWith (dir + '/'));
	}

	int EnumValue (const QMetaEnum &me, const QString &set, bool *ok)
	{
		QStringList names = custom::UiSetNames (set);
		if (names.isEmpty ()) { *ok = true; return 0; }
		return me.keysToValue (names.join ('|').toLatin1 ().constData (), ok);
	}

	QSizePolicy::Policy PolicyOf (const QString &name, QSizePolicy::Policy def)
	{
		const QMetaEnum me = QMetaEnum::fromType<QSizePolicy::Policy> ();
		bool ok = false;
		int v = me.keyToValue (name.toLatin1 ().constData (), &ok);
		return ok ? (QSizePolicy::Policy)v : def;
	}

	QFont FontOf (const UiValue &v)
	{
		QFont f;
		if (!v.family.isEmpty ()) f.setFamily (v.family);
		if (v.pointSize > 0) f.setPointSize (v.pointSize);
		if (v.bold >= 0) f.setBold (v.bold);
		if (v.italic >= 0) f.setItalic (v.italic);
		return f;
	}

	QColor ColorOf (const UiValue &v)
	{
		return QColor::fromRgba (v.rgba);
	}

	QList<int> Ints (const QString &list)
	{
		QList<int> out;
		for (const QString &s : list.split (',', Qt::SkipEmptyParts)) out << s.trimmed ().toInt ();
		return out;
	}

	int IntProp (const UiLayout &l, const char *name, int def)
	{
		const UiValue *v = l.Prop (name);
		return (v && (v->type == UiValue::NUMBER || v->type == UiValue::DOUBLE) ? v->Int () : def);
	}

}

// ---------------------------------------------------------------------------------------------------------
// pictures

bool ReadQrc (const QString &qrcFile, BuildEnv &env, QString &err)
{
	const QString path = ResolveFile (env, qrcFile);
	if (path.isEmpty ()) { err = qrcFile + ": not found inside the skin folder"; return false; }
	QFile f (path);
	if (!f.open (QIODevice::ReadOnly) || f.size () > (1 << 20)) { err = qrcFile + ": can't be read"; return false; }
	const QString base = QFileInfo (path).absolutePath ();
	QXmlStreamReader x (&f);
	QString prefix;
	int n = 0;
	while (!x.atEnd ()) {
		x.readNext ();
		if (!x.isStartElement ()) continue;
		if (x.name () == u"qresource") {
			prefix = x.attributes ().value ("prefix").toString ().trimmed ();
			if (!prefix.startsWith ('/')) prefix.prepend ('/');
			while (prefix.size () > 1 && prefix.endsWith ('/')) prefix.chop (1);
		} else if (x.name () == u"file") {
			const QString alias = x.attributes ().value ("alias").toString ().trimmed ();
			const QString file = x.readElementText ().trimmed ();
			if (file.isEmpty () || ++n > 4000) continue;
			const QString name = ":" + (prefix == "/" ? QString () : prefix) + "/" + (alias.isEmpty () ? file : alias);
			const QString c = QFileInfo (QDir (base).absoluteFilePath (file)).canonicalFilePath ();
			if (!c.isEmpty () && Inside (c, env.skinDir) && QFileInfo (c).isFile ()) env.qrc[QDir::cleanPath (name)] = c;
		}
	}
	if (x.hasError ()) { err = qrcFile + QString (": line %1: %2").arg (x.lineNumber ()).arg (x.errorString ()); return false; }
	return true;
}

QPixmap LoadPixmap (const BuildEnv &env, const QString &name)
{
	const QString path = ResolveFile (env, name);
	if (path.isEmpty ()) return QPixmap ();
	QImageReader r (path);
	const QSize s = r.size ();
	if (!s.isValid () || s.width () > MAX_PICTURE || s.height () > MAX_PICTURE) {
		env.Warn (name + ": not a picture of at most 4096 x 4096");
		return QPixmap ();
	}
	QImage img = r.read ();
	if (img.isNull ()) {
		env.Warn (name + ": " + r.errorString ());
		return QPixmap ();
	}
	return QPixmap::fromImage (img);
}

QIcon LoadIcon (const BuildEnv &env, const UiValue &v)
{
	QIcon icon;
	static const QIcon::Mode modes[4] = {QIcon::Normal, QIcon::Disabled, QIcon::Active, QIcon::Selected};
	for (int i = 0; i < 8 && i < v.list.size (); i++) {
		if (v.list[i].isEmpty ()) continue;
		QPixmap pm = LoadPixmap (env, v.list[i]);
		if (!pm.isNull ()) icon.addPixmap (pm, modes[i / 2], (i % 2) ? QIcon::On : QIcon::Off);
	}
	return icon;
}

namespace {

	// reads tags the way Qt's rich text parser does (qtexthtmlparser.cpp), to find what it would load
	struct HtmlScan {
		const QString &s;
		qsizetype pos = 0, len = 0;
		explicit HtmlScan (const QString &t): s (t), len (t.size ()) {}
		bool At (QChar c, qsizetype ahead = 0) const { return pos + ahead < len && s[pos + ahead] == c; }
		void Space () { while (pos < len && s[pos].isSpace () && s[pos] != QChar::ParagraphSeparator) pos++; }

		QString Entity ()
		{
			const qsizetype recover = pos;
			qsizetype n = 0;
			while (pos < len) {
				const QChar c = s[pos++];
				if (c.isSpace () || pos - recover > 9) { pos = recover; return "&"; }
				if (c == ';') break;
				n++;
			}
			if (n) {
				const QStringView e = QStringView (s).mid (recover, n);
				static const std::pair<const char*, char> ascii[] = {{"AMP", '&'}, {"GT", '>'}, {"LT", '<'}, {"QUOT", '"'},
					{"amp", '&'}, {"apos", '\''}, {"gt", '>'}, {"lt", '<'}, {"percnt", '%'}, {"quot", '"'}};
				for (const auto &[name, c] : ascii) if (e == QLatin1String (name)) return QString (QChar (c));
				if (e.size () > 1 && e[0] == '#') {
					QStringView d = e.mid (1);
					int base = 10;
					if (d[0].toLower () == 'x') { d = d.mid (1); base = 16; }
					bool ok = false;
					const uint uc = d.toUInt (&ok, base);
					if (ok) {
						const char32_t c = (uc >= 0x80 && uc < 0xa0) || uc > 0x10ffff ? 0xfffd : uc; // never a letter
						return QString::fromUcs4 (&c, 1);
					}
				} else if (std::all_of (e.begin (), e.end (), [](QChar c) { return c.isLetterOrNumber (); }))
					return QString (QChar (0xfffd)); // a named entity is never an ASCII letter
			}
			pos = recover;
			return "&";
		}

		QString Word ()
		{
			QString w;
			if (At ('"')) {
				pos++;
				while (pos < len) {
					const QChar c = s[pos++];
					if (c == '"') break;
					if (c == '&') w += Entity (); else w += c;
				}
			} else if (At ('\'')) {
				pos++;
				while (pos < len) {
					const QChar c = s[pos++];
					if (c == '\'' && s[pos - 2] != '\\') break;
					w += c;
				}
			} else {
				while (pos < len) {
					const QChar c = s[pos++];
					if (c == '>' || (c == '/' && At ('>')) || c == '<' || c == '=' || c.isSpace ()) { pos--; break; }
					if (c == '&') w += Entity (); else w += c;
				}
			}
			return w;
		}
	};

	QString Quoted (QString v)
	{
		v.replace ("&", "&amp;").replace ("\"", "&quot;");
		return "\"" + v + "\"";
	}

	bool UnsafeCss (QStringView raw) // entities could spell anything; @import reads a file
	{
		return raw.contains ('&') || raw.contains ('@');
	}

	bool HasUrl (QStringView raw) // Qt's CSS drops backslashes from names: u\rl( is url(
	{
		QString d = raw.toString ();
		d.remove ('\\');
		return d.contains (QLatin1String ("url"), Qt::CaseInsensitive);
	}

}

QString SafeText (const BuildEnv &env, const QString &s)
{
	if (!s.contains ('<')) return s; // no tags, no rich text
	struct Edit { qsizetype from, to; QString with; };
	std::vector<Edit> edits;
	HtmlScan h (s);
	bool warn = false;
	// a style element's text: Qt joins it across comments and close tags that close nothing, and reads it as CSS at each close tag
	bool inStyle = false, broken = false;
	qsizetype styleText = -1;
	std::vector<std::pair<qsizetype, qsizetype>> parts;
	auto part = [&](qsizetype to) { if (inStyle && to > styleText) parts.push_back ({styleText, to}); };
	auto endStyle = [&]() {
		if (!inStyle) return;
		inStyle = false;
		QString css;
		for (const auto &[a, b] : parts) css += QStringView (s).mid (a, b - a);
		if (UnsafeCss (css) || (broken && !css.trimmed ().isEmpty ())) { // a url( could be split across the pieces
			for (const auto &[a, b] : parts) edits.push_back ({a, b, {}});
			warn = true;
		} else if (parts.size () == 1 && HasUrl (css)) edits.push_back ({parts[0].first, parts[0].second, RewriteStyle (env, css)});
		parts.clear ();
		broken = false;
	};
	while (h.pos < h.len) {
		if (s[h.pos++] != '<') continue;
		const qsizetype lt = h.pos - 1;
		part (lt);
		h.Space ();
		if (h.At ('!')) {
			h.pos++;
			if (h.At ('-') && h.At ('-', 1)) {
				const qsizetype end = s.indexOf ("-->", h.pos + 2);
				h.pos = (end >= 0 ? end + 3 : h.len);
			} else while (h.pos < h.len && s[h.pos++] != '>') {}
			if (inStyle) { broken = true; styleText = h.pos; }
			continue;
		}
		if (h.At ('/')) {
			h.pos++;
			const QString name = h.Word ().toLower ().trimmed ();
			while (h.pos < h.len && s[h.pos++] != '>') {}
			if (inStyle && name == "style") endStyle ();
			else if (inStyle) { broken = true; styleText = h.pos; }
			continue;
		}
		endStyle (); // a new element ends the style element's text
		const QString tag = h.Word ().toLower ();
		std::vector<Edit> attrs;
		if (h.pos < h.len && s[h.pos].isSpace ()) {
			while (h.pos < h.len) {
				h.Space ();
				if (h.At ('>') || h.At ('/')) break;
				const QString key = h.Word ().toLower ();
				const qsizetype keyEnd = h.pos;
				if (key.isEmpty ()) break;
				h.Space ();
				qsizetype from = -1;
				QString value = "1";
				if (h.At ('=')) {
					h.pos++;
					h.Space ();
					from = h.pos;
					value = h.Word ();
				}
				if (value.isEmpty ()) continue;
				const bool picture = (key == "src" || key == "source" || key == "background");
				if (!picture && key != "style") continue;
				if (from < 0) { attrs.push_back ({keyEnd, keyEnd, "=\"\""}); continue; } // a bare src loads "1"
				QString with;
				const QStringView raw = QStringView (s).mid (from, h.pos - from);
				if (picture) with = Quoted (ResolveFile (env, value));
				else if (UnsafeCss (raw)) { with = "\"\""; warn = true; }
				else if (HasUrl (raw)) with = Quoted (RewriteStyle (env, value));
				else continue; // nothing in it reads a file
				attrs.push_back ({from, h.pos, with});
			}
		}
		while (h.pos < h.len && s[h.pos] != '>') h.pos++;
		h.pos = std::min (h.pos + 1, h.len);
		if (tag == "link") { edits.push_back ({lt, h.pos, {}}); warn = true; continue; } // style sheets from files
		edits.insert (edits.end (), attrs.begin (), attrs.end ());
		if (tag == "style") { inStyle = true; styleText = h.pos; }
	}
	part (h.len);
	endStyle ();
	if (warn && !env.warned.contains ("<link")) { env.warned.insert ("<link"); env.Warn ("rich text: style sheet links, and styles with & or @ or in pieces, are removed"); }
	QString out;
	qsizetype last = 0;
	for (const Edit &e : edits) {
		out += QStringView (s).mid (last, e.from - last);
		out += e.with;
		last = e.to;
	}
	out += QStringView (s).mid (last);
	return out;
}

QVariant SafeValue (const BuildEnv &env, QObject *w, const QByteArray &name, const QVariant &v)
{
	if (name == "styleSheet") return RewriteStyle (env, v.toString ());
	if (name == "toolTip" || name == "whatsThis" || (name == "text" && qobject_cast<QLabel*> (w))) return SafeText (env, v.toString ());
	if (name == "openExternalLinks") {
		if (v.toBool ()) env.Warn (w->objectName () + ": openExternalLinks is not available in launcher forms");
		return false;
	}
	if (name == "textFormat") { // plain, rich or auto only: Markdown has its own pictures
		bool ok = false;
		int f = -1;
		if (v.typeId () == QMetaType::QString) {
			const QString s = v.toString ().trimmed ();
			f = s.toInt (&ok, 0);
			if (!ok) f = QMetaEnum::fromType<Qt::TextFormat> ().keysToValue (custom::UiSetNames (s).join ('|').toLatin1 ().constData (), &ok);
		} else f = v.toInt (&ok);
		if (ok && (f == Qt::PlainText || f == Qt::RichText || f == Qt::AutoText)) return f;
		env.Warn (w->objectName () + ": textFormat " + v.toString () + " is shown as plain text");
		return int (Qt::PlainText);
	}
	return v;
}

// ---------------------------------------------------------------------------------------------------------
// classes

namespace {
	const char *const PAINTED[] = {"Starfield", "Planet", "ScenarioThumb", "GridBackdrop", "MiniOrbit", "OrbitDiagram"};
	const char *const STANDARD[] = {"QWidget", "QFrame", "Line", "QLabel", "QPushButton", "QToolButton", "QCheckBox",
		"QRadioButton", "QLineEdit", "QStackedWidget", "QScrollArea", "QGroupBox", "QTabWidget", "QProgressBar"};
}

bool PaintedClass (const QString &cls)
{
	for (const char *c : PAINTED) if (cls == QLatin1String (c)) return true;
	return false;
}

bool KnownClass (const QString &cls)
{
	for (const char *c : STANDARD) if (cls == QLatin1String (c)) return true;
	return PaintedClass (cls);
}

QWidget *NewWidget (const QString &cls, QWidget *parent)
{
	if (cls == "QWidget") return new QWidget (parent);
	if (cls == "QFrame") return new QFrame (parent);
	if (cls == "Line") {
		auto *f = new QFrame (parent);
		f->setFrameShape (QFrame::HLine);
		f->setFrameShadow (QFrame::Sunken);
		return f;
	}
	if (cls == "QLabel") return new FormLabel (parent);
	if (cls == "QPushButton") return new FormButton (parent);
	if (cls == "QToolButton") return new FormToolButton (parent);
	if (cls == "QCheckBox") return new FormCheckBox (parent);
	if (cls == "QRadioButton") return new FormRadioButton (parent);
	if (cls == "QLineEdit") return new QLineEdit (parent);
	if (cls == "QStackedWidget") return new QStackedWidget (parent);
	if (cls == "QScrollArea") return new FormScrollArea (parent);
	if (cls == "QGroupBox") return new QGroupBox (parent);
	if (cls == "QTabWidget") return new QTabWidget (parent);
	if (cls == "QProgressBar") return new QProgressBar (parent);
	if (cls == "Starfield") return new Starfield (parent);
	if (cls == "Planet") return new Planet (parent);
	if (cls == "ScenarioThumb") return new ScenarioThumb (parent);
	if (cls == "GridBackdrop") return new GridBackdrop (parent);
	if (cls == "MiniOrbit") return new MiniOrbit (parent);
	if (cls == "OrbitDiagram") return new OrbitDiagram (parent);
	return nullptr;
}

// ---------------------------------------------------------------------------------------------------------
// properties

int AlignmentOf (const QString &set)
{
	bool ok = false;
	int v = EnumValue (QMetaEnum::fromType<Qt::Alignment> (), set, &ok);
	return ok ? v : 0;
}

QVariant NaturalValue (const BuildEnv &env, const UiValue &v)
{
	switch (v.type) {
	case UiValue::STRING: return v.str;
	case UiValue::NUMBER: return v.Int ();
	case UiValue::DOUBLE: return v.num;
	case UiValue::BOOL: return v.flag;
	case UiValue::RECT: return v.rect;
	case UiValue::SIZE: return v.rect.size ();
	case UiValue::ENUM: case UiValue::SET: case UiValue::CURSOR: case UiValue::URL: return v.str;
	case UiValue::FONT: return FontOf (v);
	case UiValue::PIXMAP: return LoadPixmap (env, v.str);
	case UiValue::ICON: return LoadIcon (env, v);
	case UiValue::STRINGLIST: return v.list;
	case UiValue::COLOR: return ColorOf (v);
	default: return QVariant ();
	}
}

bool SetStdProperty (const BuildEnv &env, QWidget *w, const QString &cls, const QString &name, const UiValue &v, QString &why)
{
	if (cls == "Line" && name == "orientation") {
		auto *f = static_cast<QFrame*> (w);
		f->setFrameShape (custom::UiSetNames (v.str).value (0) == "Vertical" ? QFrame::VLine : QFrame::HLine);
		return true;
	}
	const QMetaObject *mo = w->metaObject ();
	const int idx = mo->indexOfProperty (name.toLatin1 ().constData ());
	if (idx < 0) { why = "no such property"; return false; }
	const QMetaProperty mp = mo->property (idx);
	if (!mp.isWritable ()) { why = "read-only"; return false; }
	const int t = mp.metaType ().id ();
	QVariant val;
	switch (v.type) {
	case UiValue::STRING:
		val = v.str;
		break;
	case UiValue::NUMBER: case UiValue::DOUBLE:
		if (t == QMetaType::Int || t == QMetaType::UInt || t == QMetaType::LongLong) val = v.Int ();
		else val = v.num;
		break;
	case UiValue::BOOL: val = v.flag; break;
	case UiValue::RECT: val = v.rect; break;
	case UiValue::SIZE: val = v.rect.size (); break;
	case UiValue::ENUM: case UiValue::SET: {
		if (!mp.isEnumType ()) { why = "not an enum property"; return false; }
		bool ok = false;
		int e = EnumValue (mp.enumerator (), v.str, &ok);
		if (!ok) { why = "unknown value " + v.str; return false; }
		val = e;
		break;
	}
	case UiValue::FONT: val = FontOf (v); break;
	case UiValue::PIXMAP: {
		QPixmap pm = LoadPixmap (env, v.str);
		if (pm.isNull ()) return true; // warned
		if (t == QMetaType::QIcon) val = QIcon (pm);
		else val = pm;
		break;
	}
	case UiValue::ICON: val = LoadIcon (env, v); break;
	case UiValue::CURSOR: {
		bool ok = false;
		int shape = v.str.toInt (&ok);
		if (!ok) shape = QMetaEnum::fromType<Qt::CursorShape> ().keyToValue (v.str.toLatin1 ().constData (), &ok);
		if (!ok) { why = "unknown cursor " + v.str; return false; }
		val = QCursor ((Qt::CursorShape)shape);
		break;
	}
	case UiValue::SIZEPOLICY: {
		QSizePolicy sp (PolicyOf (v.hPolicy, QSizePolicy::Preferred), PolicyOf (v.vPolicy, QSizePolicy::Preferred));
		sp.setHorizontalStretch (v.hStretch);
		sp.setVerticalStretch (v.vStretch);
		val = QVariant::fromValue (sp);
		break;
	}
	case UiValue::COLOR: val = ColorOf (v); break;
	case UiValue::STRINGLIST: val = v.list; break;
	case UiValue::URL: val = QUrl (v.str); break;
	default: why = "value type not supported"; return false;
	}
	val = SafeValue (env, w, name.toLatin1 (), val);
	if (!w->setProperty (name.toLatin1 ().constData (), val)) { why = "the value doesn't fit"; return false; }
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// layouts

QLayout *MakeLayout (const BuildEnv &env, const UiLayout &l, QWidget *owner, const std::vector<QWidget*> &built,
	const std::vector<bool> &isTemplate, TemplateSlot &slot, bool top)
{
	QLayout *lay = nullptr;
	QBoxLayout *box = nullptr;
	QGridLayout *grid = nullptr;
	QFormLayout *form = nullptr;
	QWidget *parent = (top ? owner : nullptr);
	if (l.cls == "QHBoxLayout") lay = box = new QHBoxLayout (parent);
	else if (l.cls == "QGridLayout") lay = grid = new QGridLayout (parent);
	else if (l.cls == "QFormLayout") lay = form = new QFormLayout (parent);
	else {
		if (l.cls != "QVBoxLayout") env.Warn ("layout class " + l.cls + " is built as a QVBoxLayout");
		lay = box = new QVBoxLayout (parent);
	}
	lay->setObjectName (l.name);
	const int m = IntProp (l, "margin", -1);
	QMargins mm = (m >= 0 ? QMargins (m, m, m, m) : top ? lay->contentsMargins () : QMargins (0, 0, 0, 0)); // Qt's defaults, as Designer
	mm.setLeft (IntProp (l, "leftMargin", mm.left ()));
	mm.setTop (IntProp (l, "topMargin", mm.top ()));
	mm.setRight (IntProp (l, "rightMargin", mm.right ()));
	mm.setBottom (IntProp (l, "bottomMargin", mm.bottom ()));
	lay->setContentsMargins (mm);
	if (const int s = IntProp (l, "spacing", -1); s >= 0) lay->setSpacing (s);
	if (grid) {
		if (const int s = IntProp (l, "horizontalSpacing", -1); s >= 0) grid->setHorizontalSpacing (s);
		if (const int s = IntProp (l, "verticalSpacing", -1); s >= 0) grid->setVerticalSpacing (s);
	}
	if (form) {
		if (const int s = IntProp (l, "horizontalSpacing", -1); s >= 0) form->setHorizontalSpacing (s);
		if (const int s = IntProp (l, "verticalSpacing", -1); s >= 0) form->setVerticalSpacing (s);
	}
	if (const UiValue *sc = l.Prop ("sizeConstraint"); sc && sc->type == UiValue::ENUM) {
		bool ok = false;
		int v = QMetaEnum::fromType<QLayout::SizeConstraint> ().keyToValue (custom::UiSetNames (sc->str).value (0).toLatin1 ().constData (), &ok);
		if (ok) lay->setSizeConstraint ((QLayout::SizeConstraint)v);
	}
	int added = 0;
	for (const UiLayoutItem &it : l.items) {
		const Qt::Alignment al = (Qt::Alignment)AlignmentOf (it.alignment);
		const int row = std::max (0, it.row), col = std::max (0, it.col);
		const QFormLayout::ItemRole role = (it.colSpan > 1 ? QFormLayout::SpanningRole : col == 0 ? QFormLayout::LabelRole : QFormLayout::FieldRole);
		if (it.kind == UiLayoutItem::WIDGET) {
			if (it.widget < 0 || it.widget >= (int)built.size ()) continue;
			if (isTemplate[it.widget]) {
				if (top && slot.insertAt < 0) {
					slot.insertAt = added;
					slot.row = row;
					slot.col = col;
					slot.align = (int)al;
				} else if (!top) env.Warn ("a template inside a nested layout of its list is left out");
				continue;
			}
			QWidget *c = built[it.widget];
			if (!c) continue;
			if (box) box->addWidget (c, 0, al);
			else if (grid) grid->addWidget (c, row, col, it.rowSpan, it.colSpan, al);
			else if (form) form->setWidget (row, role, c);
			added++;
		} else if (it.kind == UiLayoutItem::LAYOUT && !it.sub.empty ()) {
			TemplateSlot inner;
			QLayout *sub = MakeLayout (env, it.sub[0], owner, built, isTemplate, inner, false);
			if (box) box->addLayout (sub);
			else if (grid) grid->addLayout (sub, row, col, it.rowSpan, it.colSpan, al);
			else if (form) form->setLayout (row, role, sub);
			added++;
		} else if (it.kind == UiLayoutItem::SPACER) {
			const UiValue *o = it.Prop ("orientation");
			const bool vertical = (o && custom::UiSetNames (o->str).value (0) == "Vertical");
			const UiValue *st = it.Prop ("sizeType");
			const QSizePolicy::Policy pol = (st ? PolicyOf (custom::UiSetNames (st->str).value (0), QSizePolicy::Expanding) : QSizePolicy::Expanding);
			const UiValue *sh = it.Prop ("sizeHint");
			const QSize hint = (sh && sh->type == UiValue::SIZE ? sh->rect.size () : QSize (20, 20));
			auto *sp = new QSpacerItem (hint.width (), hint.height (), vertical ? QSizePolicy::Minimum : pol, vertical ? pol : QSizePolicy::Minimum);
			if (box) box->addItem (sp);
			else if (grid) grid->addItem (sp, row, col, it.rowSpan, it.colSpan, al);
			else if (form) form->setItem (row, role, sp);
			else delete sp;
			added++;
		}
	}
	if (box) {
		QList<int> st = Ints (l.stretch);
		for (int i = 0; i < st.size () && i < box->count (); i++) box->setStretch (i, st[i]);
	}
	if (grid) {
		QList<int> rs = Ints (l.rowStretch), cs = Ints (l.colStretch), rm = Ints (l.rowMinHeight), cm = Ints (l.colMinWidth);
		const int MAX_CELLS = 1000; // as UiForm's rows and columns; a long list would grow the grid
		for (int i = 0; i < rs.size () && i < MAX_CELLS; i++) grid->setRowStretch (i, rs[i]);
		for (int i = 0; i < cs.size () && i < MAX_CELLS; i++) grid->setColumnStretch (i, cs[i]);
		for (int i = 0; i < rm.size () && i < MAX_CELLS; i++) grid->setRowMinimumHeight (i, rm[i]);
		for (int i = 0; i < cm.size () && i < MAX_CELLS; i++) grid->setColumnMinimumWidth (i, cm[i]);
	}
	return lay;
}

// ---------------------------------------------------------------------------------------------------------
// anchors

QSize DesignSize (const UiWidget &u)
{
	if (const UiValue *g = u.Prop ("geometry"); g && g->type == UiValue::RECT) return g->rect.size ();
	const UiValue *mn = u.Prop ("minimumSize"), *mx = u.Prop ("maximumSize");
	if (mn && mx && mn->type == UiValue::SIZE && mx->type == UiValue::SIZE && mn->rect.size () == mx->rect.size ()) return mn->rect.size ();
	return QSize ();
}

Anchors::Anchors (QWidget *p, const QSize &d): QObject (p), parent (p), design (d)
{
	p->installEventFilter (this);
}

int Anchors::Parse (const QString &spec)
{
	int f = 0;
	for (const QString &w : spec.toLower ().split (QRegularExpression ("[\\s,|]+"), Qt::SkipEmptyParts)) {
		if (w == "left") f |= LEFT;
		else if (w == "right") f |= RIGHT;
		else if (w == "top") f |= TOP;
		else if (w == "bottom") f |= BOTTOM;
		else if (w == "hcenter") f |= HCENTER;
		else if (w == "vcenter") f |= VCENTER;
		else if (w == "fill") f |= LEFT | RIGHT | TOP | BOTTOM;
	}
	return f;
}

void Anchors::Add (QWidget *w, int flags, const QRect &d)
{
	items.push_back ({w, flags, d});
}

void Anchors::Apply ()
{
	if (!parent || !design.isValid ()) return;
	const int W = parent->width (), H = parent->height ();
	for (auto &it : items) {
		if (!it.w) continue;
		const QRect &d = it.design;
		const int dl = d.x (), dr = design.width () - d.x () - d.width (), dt = d.y (), db = design.height () - d.y () - d.height ();
		int x = it.w->x (), y = it.w->y (), w = it.w->width (), h = it.w->height ();
		if ((it.flags & LEFT) && (it.flags & RIGHT)) { x = dl; w = std::max (0, W - dl - dr); }
		else if (it.flags & RIGHT) x = W - dr - w;
		else if (it.flags & HCENTER) x = (W - w) / 2 + (d.x () + d.width () / 2 - design.width () / 2);
		else x = dl;
		if ((it.flags & TOP) && (it.flags & BOTTOM)) { y = dt; h = std::max (0, H - dt - db); }
		else if (it.flags & BOTTOM) y = H - db - h;
		else if (it.flags & VCENTER) y = (H - h) / 2 + (d.y () + d.height () / 2 - design.height () / 2);
		else y = dt;
		if (it.w->geometry () != QRect (x, y, w, h)) it.w->setGeometry (x, y, w, h);
	}
}

bool Anchors::eventFilter (QObject *obj, QEvent *e)
{
	if (obj == parent && (e->type () == QEvent::Resize || e->type () == QEvent::Show)) Apply ();
	return false;
}

}
