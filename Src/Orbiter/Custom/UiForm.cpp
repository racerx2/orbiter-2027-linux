
// custom: launcher layouts and forms skins; the subset of Qt Designer's .ui format they use (reader and writer)

#include "UiForm.h"
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>

namespace custom {

const char *const UiIconStates[8] = {"normaloff", "normalon", "disabledoff", "disabledon", "activeoff", "activeon", "selectedoff", "selectedon"};

namespace {

	const int MAX_DEPTH = 64;

	struct Reader {
		QXmlStreamReader &x;
		UiForm &form;
		const UiLimits &lim;
		QString &err;
		int items = 0; // layout items and sub-layouts of the whole form
	};

	UiValue Make (UiValue::Type t)
	{
		UiValue v;
		v.type = t;
		return v;
	}

	const UiValue *Find (const std::vector<std::pair<QString, UiValue>> &l, const QString &n)
	{
		for (const auto &p : l)
			if (p.first == n) return &p.second;
		return nullptr;
	}

	void Put (std::vector<std::pair<QString, UiValue>> &l, const QString &n, const UiValue &v)
	{
		for (auto &p : l)
			if (p.first == n) { p.second = v; return; }
		l.push_back ({n, v});
	}

	int ToInt (const QString &s)
	{
		bool ok = false;
		double d = s.trimmed ().toDouble (&ok);
		return (ok && std::isfinite (d) ? (int)std::lround (std::clamp (d, -1e6, 1e6)) : 0);
	}

	bool ToBool (const QString &s)
	{
		return !s.trimmed ().compare ("true", Qt::CaseInsensitive);
	}

	QString TooBig (int bytes)
	{
		return (bytes % (1 << 20) ? QString ("the file is larger than %1 KiB").arg (bytes / 1024) : QString ("the file is larger than %1 MiB").arg (bytes >> 20));
	}

	QString LastPart (QString s)
	{
		s = s.trimmed ();
		int k = s.lastIndexOf ("::");
		return (k >= 0 ? s.mid (k + 2) : s);
	}

	// x, y, width, height of a <rect> or <size>
	void ReadRect (QXmlStreamReader &x, QRect &r)
	{
		int v[4] = {0, 0, 0, 0};
		while (x.readNextStartElement ()) {
			const QStringView n = x.name ();
			int k = (n == u"x" ? 0 : n == u"y" ? 1 : n == u"width" ? 2 : n == u"height" ? 3 : -1);
			if (k >= 0) v[k] = ToInt (x.readElementText ());
			else x.skipCurrentElement ();
		}
		r = QRect (v[0], v[1], std::max (0, v[2]), std::max (0, v[3]));
	}

	void ReadFont (QXmlStreamReader &x, UiValue &v)
	{
		while (x.readNextStartElement ()) {
			const QStringView n = x.name ();
			if (n == u"family") v.family = x.readElementText ().trimmed ();
			else if (n == u"pointsize") v.pointSize = std::max (0, ToInt (x.readElementText ()));
			else if (n == u"bold") v.bold = ToBool (x.readElementText ());
			else if (n == u"italic") v.italic = ToBool (x.readElementText ());
			else x.skipCurrentElement ();
		}
	}

	void ReadColor (QXmlStreamReader &x, UiValue &v)
	{
		QStringView a = x.attributes ().value ("alpha");
		int c[4] = {0, 0, 0, a.isEmpty () ? 255 : ToInt (a.toString ())};
		while (x.readNextStartElement ()) {
			const QStringView n = x.name ();
			int k = (n == u"red" ? 0 : n == u"green" ? 1 : n == u"blue" ? 2 : -1);
			if (k >= 0) c[k] = ToInt (x.readElementText ());
			else x.skipCurrentElement ();
		}
		for (int &k : c) k = std::clamp (k, 0, 255);
		v.rgba = ((unsigned)c[3] << 24) | ((unsigned)c[0] << 16) | ((unsigned)c[1] << 8) | (unsigned)c[2];
	}

	// <iconset>: state elements, or Designer's older form with the path as text
	void ReadIcon (QXmlStreamReader &x, UiValue &v)
	{
		v.res = x.attributes ().value ("resource").toString ();
		v.str = x.attributes ().value ("theme").toString ();
		v.list = QStringList (8, QString ());
		QString text;
		while (!x.atEnd ()) {
			x.readNext ();
			if (x.isEndElement ()) break;
			if (x.isCharacters ()) text += x.text ();
			else if (x.isStartElement ()) {
				int k = -1;
				for (int i = 0; i < 8; i++)
					if (x.name () == QLatin1String (UiIconStates[i])) k = i;
				if (k >= 0) v.list[k] = x.readElementText (QXmlStreamReader::SkipChildElements).trimmed ();
				else x.skipCurrentElement ();
			}
		}
		if (v.list[0].isEmpty ()) v.list[0] = text.trimmed ();
	}

	void ReadPolicy (QXmlStreamReader &x, UiValue &v)
	{
		v.hPolicy = LastPart (x.attributes ().value ("hsizetype").toString ());
		v.vPolicy = LastPart (x.attributes ().value ("vsizetype").toString ());
		while (x.readNextStartElement ()) {
			const QStringView n = x.name ();
			if (n == u"horstretch") v.hStretch = std::clamp (ToInt (x.readElementText ()), 0, 255);
			else if (n == u"verstretch") v.vStretch = std::clamp (ToInt (x.readElementText ()), 0, 255);
			else x.skipCurrentElement ();
		}
	}

	// at <property> or <attribute>: its value element, then up to the end element
	void ReadProperty (QXmlStreamReader &x, UiValue &v)
	{
		bool have = false;
		while (x.readNextStartElement ()) {
			if (have) { x.skipCurrentElement (); continue; }
			have = true;
			const QStringView n = x.name ();
			if (n == u"string" || n == u"cstring") { v.type = UiValue::STRING; v.str = x.readElementText (QXmlStreamReader::SkipChildElements); }
			else if (n == u"number" || n == u"double" || n == u"float" || n == u"UInt" || n == u"longlong" || n == u"uLongLong") {
				v.type = (n == u"double" || n == u"float" ? UiValue::DOUBLE : UiValue::NUMBER);
				bool ok = false;
				double d = x.readElementText ().trimmed ().toDouble (&ok);
				v.num = (ok && std::isfinite (d) ? d : 0.0);
			}
			else if (n == u"bool") { v.type = UiValue::BOOL; v.flag = ToBool (x.readElementText ()); }
			else if (n == u"enum") { v.type = UiValue::ENUM; v.str = x.readElementText ().trimmed (); }
			else if (n == u"set") { v.type = UiValue::SET; v.str = x.readElementText ().trimmed (); }
			else if (n == u"pixmap") {
				v.type = UiValue::PIXMAP;
				v.res = x.attributes ().value ("resource").toString ();
				v.str = x.readElementText (QXmlStreamReader::SkipChildElements).trimmed ();
			}
			else if (n == u"iconset") { v.type = UiValue::ICON; ReadIcon (x, v); }
			else if (n == u"rect") { v.type = UiValue::RECT; ReadRect (x, v.rect); }
			else if (n == u"size") { v.type = UiValue::SIZE; ReadRect (x, v.rect); v.rect.moveTo (0, 0); }
			else if (n == u"font") { v.type = UiValue::FONT; ReadFont (x, v); }
			else if (n == u"stringlist") {
				v.type = UiValue::STRINGLIST;
				while (x.readNextStartElement ()) {
					if (x.name () == u"string") v.list.append (x.readElementText (QXmlStreamReader::SkipChildElements));
					else x.skipCurrentElement ();
				}
			}
			else if (n == u"color") { v.type = UiValue::COLOR; ReadColor (x, v); }
			else if (n == u"cursorShape" || n == u"cursor") { v.type = UiValue::CURSOR; v.str = LastPart (x.readElementText ()); }
			else if (n == u"sizepolicy") { v.type = UiValue::SIZEPOLICY; ReadPolicy (x, v); }
			else if (n == u"url") {
				v.type = UiValue::URL;
				while (x.readNextStartElement ()) {
					if (x.name () == u"string") v.str = x.readElementText (QXmlStreamReader::SkipChildElements).trimmed ();
					else x.skipCurrentElement ();
				}
			}
			else x.skipCurrentElement ();
		}
	}

	bool ReadWidget (Reader &r, UiWidget &w, int depth);
	bool ReadLayout (Reader &r, UiLayout &l, UiWidget &owner, int depth);

	bool AddChild (Reader &r, UiWidget &owner, int depth, int &index)
	{
		if (++r.form.widgets > r.lim.maxWidgets) { r.err = QString ("more than %1 widgets").arg (r.lim.maxWidgets); return false; }
		if (depth >= MAX_DEPTH) { r.err = "widgets nested too deep"; return false; }
		owner.children.emplace_back ();
		index = (int)owner.children.size () - 1;
		return ReadWidget (r, owner.children.back (), depth + 1);
	}

	bool ReadItem (Reader &r, UiLayoutItem &it, UiWidget &owner, int depth)
	{
		const QXmlStreamAttributes a = r.x.attributes ();
		const int MAX_CELLS = 1000; // QGridLayout keeps arrays as long as the highest row
		if (a.hasAttribute ("row")) it.row = std::clamp (ToInt (a.value ("row").toString ()), 0, MAX_CELLS - 1);
		if (a.hasAttribute ("column")) it.col = std::clamp (ToInt (a.value ("column").toString ()), 0, MAX_CELLS - 1);
		if (a.hasAttribute ("rowspan")) it.rowSpan = std::clamp (ToInt (a.value ("rowspan").toString ()), 1, MAX_CELLS);
		if (a.hasAttribute ("colspan")) it.colSpan = std::clamp (ToInt (a.value ("colspan").toString ()), 1, MAX_CELLS);
		it.alignment = a.value ("alignment").toString ();
		bool have = false;
		while (r.x.readNextStartElement ()) {
			const QStringView n = r.x.name ();
			if (have) { r.x.skipCurrentElement (); continue; }
			if (n == u"widget") {
				have = true;
				it.kind = UiLayoutItem::WIDGET;
				if (!AddChild (r, owner, depth, it.widget)) return false;
			} else if (n == u"layout") {
				have = true;
				it.kind = UiLayoutItem::LAYOUT;
				it.sub.emplace_back ();
				if (depth >= MAX_DEPTH) { r.err = "layouts nested too deep"; return false; }
				if (!ReadLayout (r, it.sub.back (), owner, depth + 1)) return false;
			} else if (n == u"spacer") {
				have = true;
				it.kind = UiLayoutItem::SPACER;
				it.name = r.x.attributes ().value ("name").toString ();
				while (r.x.readNextStartElement ()) {
					if (r.x.name () == u"property") {
						QString pn = r.x.attributes ().value ("name").toString ();
						UiValue v;
						ReadProperty (r.x, v);
						if ((int)it.props.size () >= r.lim.maxProperties) { r.err = QString ("more than %1 properties in one spacer").arg (r.lim.maxProperties); return false; }
						if (!pn.isEmpty ()) Put (it.props, pn, v);
					} else r.x.skipCurrentElement ();
				}
			} else r.x.skipCurrentElement ();
		}
		if (!have) it.kind = UiLayoutItem::SPACER; // an empty item takes no room
		return !r.x.hasError ();
	}

	bool ReadLayout (Reader &r, UiLayout &l, UiWidget &owner, int depth)
	{
		r.form.layout = true;
		const QXmlStreamAttributes a = r.x.attributes ();
		l.cls = a.value ("class").toString ();
		l.name = a.value ("name").toString ();
		l.stretch = a.value ("stretch").toString ();
		l.rowStretch = a.value ("rowstretch").toString ();
		l.colStretch = a.value ("columnstretch").toString ();
		l.rowMinHeight = a.value ("rowminimumheight").toString ();
		l.colMinWidth = a.value ("columnminimumwidth").toString ();
		while (r.x.readNextStartElement ()) {
			const QStringView n = r.x.name ();
			if (n == u"property") {
				QString pn = r.x.attributes ().value ("name").toString ();
				UiValue v;
				ReadProperty (r.x, v);
				if ((int)l.props.size () >= r.lim.maxProperties) { r.err = QString ("more than %1 properties in one layout").arg (r.lim.maxProperties); return false; }
				if (!pn.isEmpty ()) Put (l.props, pn, v);
			} else if (n == u"item") {
				if (++r.items > 4 * r.lim.maxWidgets) { r.err = QString ("more than %1 layout items").arg (4 * r.lim.maxWidgets); return false; }
				l.items.emplace_back ();
				if (!ReadItem (r, l.items.back (), owner, depth)) return false;
			} else r.x.skipCurrentElement ();
		}
		return !r.x.hasError ();
	}

	bool ReadWidget (Reader &r, UiWidget &w, int depth)
	{
		QXmlStreamReader &x = r.x;
		w.cls = x.attributes ().value ("class").toString ();
		w.name = x.attributes ().value ("name").toString ();
		while (x.readNextStartElement ()) {
			const QStringView n = x.name ();
			if (n == u"property" || n == u"attribute") {
				QString name = x.attributes ().value ("name").toString ();
				bool dyn = (x.attributes ().value ("stdset") == u"0");
				bool attr = (n == u"attribute");
				UiValue v;
				ReadProperty (x, v);
				if ((int)(w.props.size () + w.dyn.size () + w.attrs.size ()) >= r.lim.maxProperties) { r.err = QString ("more than %1 properties in one widget").arg (r.lim.maxProperties); return false; }
				if (!name.isEmpty ()) Put (attr ? w.attrs : dyn ? w.dyn : w.props, name, v);
			} else if (n == u"widget") {
				int k;
				if (!AddChild (r, w, depth, k)) return false;
			} else if (n == u"layout") {
				if (!w.layout.empty ()) { r.form.layout = true; x.skipCurrentElement (); continue; }
				w.layout.emplace_back ();
				if (!ReadLayout (r, w.layout.back (), w, depth)) return false;
			} else if (n == u"zorder") {
				w.zorder.append (x.readElementText ().trimmed ());
			} else {
				x.skipCurrentElement ();
			}
		}
		return !x.hasError ();
	}

	void ReadCustoms (QXmlStreamReader &x, UiForm &form)
	{
		while (x.readNextStartElement ()) {
			if (x.name () != u"customwidget") { x.skipCurrentElement (); continue; }
			UiCustom c;
			while (x.readNextStartElement ()) {
				const QStringView n = x.name ();
				if (n == u"class") c.cls = x.readElementText ().trimmed ();
				else if (n == u"extends") c.extends = x.readElementText ().trimmed ();
				else if (n == u"container") c.container = (ToInt (x.readElementText ()) != 0);
				else x.skipCurrentElement ();
			}
			if (!c.cls.isEmpty () && form.customs.size () < 200) form.customs.push_back (c);
		}
	}

	void WriteValue (QXmlStreamWriter &x, const UiValue &v)
	{
		auto rect = [&x](const char *el, const QRect &r, bool pos) {
			x.writeStartElement (el);
			if (pos) {
				x.writeTextElement ("x", QString::number (r.x ()));
				x.writeTextElement ("y", QString::number (r.y ()));
			}
			x.writeTextElement ("width", QString::number (r.width ()));
			x.writeTextElement ("height", QString::number (r.height ()));
			x.writeEndElement ();
		};
		switch (v.type) {
		case UiValue::STRING: x.writeTextElement ("string", v.str); break;
		case UiValue::NUMBER: x.writeTextElement ("number", QString::number (v.Int ())); break;
		case UiValue::DOUBLE: x.writeTextElement ("double", QString::number (v.num, 'g', 17)); break;
		case UiValue::BOOL: x.writeTextElement ("bool", v.flag ? "true" : "false"); break;
		case UiValue::RECT: rect ("rect", v.rect, true); break;
		case UiValue::SIZE: rect ("size", v.rect, false); break;
		case UiValue::ENUM: x.writeTextElement ("enum", v.str); break;
		case UiValue::SET: x.writeTextElement ("set", v.str); break;
		case UiValue::PIXMAP:
			x.writeStartElement ("pixmap");
			if (!v.res.isEmpty ()) x.writeAttribute ("resource", v.res);
			x.writeCharacters (v.str);
			x.writeEndElement ();
			break;
		case UiValue::FONT:
			x.writeStartElement ("font");
			if (!v.family.isEmpty ()) x.writeTextElement ("family", v.family);
			if (v.pointSize > 0) x.writeTextElement ("pointsize", QString::number (v.pointSize));
			if (v.bold >= 0) x.writeTextElement ("bold", v.bold ? "true" : "false");
			if (v.italic >= 0) x.writeTextElement ("italic", v.italic ? "true" : "false");
			x.writeEndElement ();
			break;
		case UiValue::STRINGLIST:
			x.writeStartElement ("stringlist");
			for (const QString &s : v.list) x.writeTextElement ("string", s);
			x.writeEndElement ();
			break;
		case UiValue::COLOR:
			x.writeStartElement ("color");
			x.writeAttribute ("alpha", QString::number ((v.rgba >> 24) & 255));
			x.writeTextElement ("red", QString::number ((v.rgba >> 16) & 255));
			x.writeTextElement ("green", QString::number ((v.rgba >> 8) & 255));
			x.writeTextElement ("blue", QString::number (v.rgba & 255));
			x.writeEndElement ();
			break;
		case UiValue::ICON:
			x.writeStartElement ("iconset");
			if (!v.res.isEmpty ()) x.writeAttribute ("resource", v.res);
			if (!v.str.isEmpty ()) x.writeAttribute ("theme", v.str);
			for (int i = 0; i < 8 && i < v.list.size (); i++)
				if (!v.list[i].isEmpty ()) x.writeTextElement (UiIconStates[i], v.list[i]);
			x.writeEndElement ();
			break;
		case UiValue::CURSOR: x.writeTextElement ("cursorShape", v.str); break;
		case UiValue::SIZEPOLICY:
			x.writeStartElement ("sizepolicy");
			x.writeAttribute ("hsizetype", v.hPolicy);
			x.writeAttribute ("vsizetype", v.vPolicy);
			x.writeTextElement ("horstretch", QString::number (v.hStretch));
			x.writeTextElement ("verstretch", QString::number (v.vStretch));
			x.writeEndElement ();
			break;
		case UiValue::URL:
			x.writeStartElement ("url");
			x.writeTextElement ("string", v.str);
			x.writeEndElement ();
			break;
		default: x.writeTextElement ("string", QString ()); break;
		}
	}

	void WriteWidget (QXmlStreamWriter &x, const UiWidget &w)
	{
		x.writeStartElement ("widget");
		x.writeAttribute ("class", w.cls);
		x.writeAttribute ("name", w.name);
		for (const auto &[n, v] : w.props) {
			if (v.type == UiValue::NONE) continue;
			x.writeStartElement ("property");
			x.writeAttribute ("name", n);
			WriteValue (x, v);
			x.writeEndElement ();
		}
		for (const auto &[n, v] : w.dyn) {
			if (v.type == UiValue::NONE) continue;
			x.writeStartElement ("property");
			x.writeAttribute ("name", n);
			x.writeAttribute ("stdset", "0");
			WriteValue (x, v);
			x.writeEndElement ();
		}
		for (const auto &[n, v] : w.attrs) {
			if (v.type == UiValue::NONE) continue;
			x.writeStartElement ("attribute");
			x.writeAttribute ("name", n);
			WriteValue (x, v);
			x.writeEndElement ();
		}
		for (const auto &c : w.children)
			WriteWidget (x, c);
		for (const auto &z : w.zorder)
			x.writeTextElement ("zorder", z);
		x.writeEndElement ();
	}

}

int UiValue::Int () const { return (int)std::lround (std::clamp (num, -1e6, 1e6)); }
UiValue UiValue::String (const QString &s) { UiValue v = Make (STRING); v.str = s; return v; }
UiValue UiValue::Number (int n) { UiValue v = Make (NUMBER); v.num = n; return v; }
UiValue UiValue::Double (double d) { UiValue v = Make (DOUBLE); v.num = d; return v; }
UiValue UiValue::Bool (bool b) { UiValue v = Make (BOOL); v.flag = b; return v; }
UiValue UiValue::Rect (const QRect &r) { UiValue v = Make (RECT); v.rect = r; return v; }
UiValue UiValue::Size (int w, int h) { UiValue v = Make (SIZE); v.rect = QRect (0, 0, w, h); return v; }
UiValue UiValue::Enum (const QString &e) { UiValue v = Make (ENUM); v.str = e; return v; }
UiValue UiValue::Set (const QString &s) { UiValue v = Make (SET); v.str = s; return v; }
UiValue UiValue::Pixmap (const QString &p) { UiValue v = Make (PIXMAP); v.str = p; return v; }
UiValue UiValue::StringList (const QStringList &l) { UiValue v = Make (STRINGLIST); v.list = l; return v; }
UiValue UiValue::Color (unsigned argb) { UiValue v = Make (COLOR); v.rgba = argb; return v; }

const UiValue *UiLayoutItem::Prop (const QString &n) const { return Find (props, n); }
const UiValue *UiLayout::Prop (const QString &n) const { return Find (props, n); }
const UiValue *UiWidget::Prop (const QString &n) const { return Find (props, n); }
const UiValue *UiWidget::Dyn (const QString &n) const { return Find (dyn, n); }
const UiValue *UiWidget::Attr (const QString &n) const { return Find (attrs, n); }
void UiWidget::SetProp (const QString &n, const UiValue &v) { Put (props, n, v); }
void UiWidget::SetDyn (const QString &n, const UiValue &v) { Put (dyn, n, v); }

const UiCustom *UiForm::Custom (const QString &c) const
{
	for (const auto &u : customs)
		if (u.cls == c) return &u;
	return nullptr;
}

std::vector<const UiWidget*> UiWidget::PaintOrder () const
{
	std::vector<const UiWidget*> low, raised;
	for (const auto &c : children)
		if (!zorder.contains (c.name)) low.push_back (&c);
	for (const auto &z : zorder)
		for (const auto &c : children)
			if (c.name == z) {
				std::erase (raised, &c); // a later raise wins
				raised.push_back (&c);
				break;
			}
	low.insert (low.end (), raised.begin (), raised.end ());
	return low;
}

bool ReadUiForm (const QByteArray &data, UiForm &form, QString &err, const UiLimits &lim)
{
	form = UiForm ();
	if (data.size () > lim.maxBytes) { err = TooBig (lim.maxBytes); return false; }
	QXmlStreamReader x (data);
	Reader r {x, form, lim, err};
	bool haveRoot = false;
	if (!x.readNextStartElement () || x.name () != u"ui") {
		err = (x.hasError () ? x.errorString () : QString ("not a Qt Designer form (no <ui> element)"));
		return false;
	}
	while (x.readNextStartElement ()) {
		const QStringView n = x.name ();
		if (n == u"widget" && !haveRoot) {
			haveRoot = true;
			if (!ReadWidget (r, form.root, 0)) {
				if (err.isEmpty ()) err = x.errorString ();
				if (err.endsWith ('.')) err.chop (1);
				err = QString ("line %1: %2").arg (x.lineNumber ()).arg (err);
				return false;
			}
		} else if (n == u"class") {
			form.cls = x.readElementText ().trimmed ();
		} else if (n == u"tabstops") {
			while (x.readNextStartElement ()) {
				if (x.name () == u"tabstop") form.tabstops.append (x.readElementText ().trimmed ());
				else x.skipCurrentElement ();
			}
		} else if (n == u"customwidgets") {
			ReadCustoms (x, form);
		} else if (n == u"resources") {
			while (x.readNextStartElement ()) {
				if (x.name () == u"include" && form.resources.size () < 16) form.resources.append (x.attributes ().value ("location").toString ());
				x.skipCurrentElement ();
			}
		} else {
			if (n == u"layout") form.layout = true;
			x.skipCurrentElement ();
		}
	}
	if (x.hasError ()) {
		QString e = x.errorString ();
		if (e.endsWith ('.')) e.chop (1);
		err = QString ("line %1: %2").arg (x.lineNumber ()).arg (e);
		return false;
	}
	if (!haveRoot) { err = "the form has no widget"; return false; }
	return true;
}

bool ReadUiFile (const QString &path, UiForm &form, QString &err, const UiLimits &lim)
{
	if (!QFileInfo (path).isFile ()) { err = "is not a file"; return false; } // a FIFO would block
	QFile f (path);
	if (!f.open (QIODevice::ReadOnly)) { err = "can't be read"; return false; }
	if (f.size () > lim.maxBytes) { err = TooBig (lim.maxBytes); return false; }
	return ReadUiForm (f.read (lim.maxBytes + 1), form, err, lim);
}

QByteArray WriteUiForm (const UiForm &form)
{
	QByteArray out;
	QXmlStreamWriter x (&out);
	x.setAutoFormatting (true);
	x.setAutoFormattingIndent (1);
	x.writeStartDocument ();
	x.writeStartElement ("ui");
	x.writeAttribute ("version", "4.0");
	x.writeTextElement ("class", form.root.name);
	WriteWidget (x, form.root);
	if (!form.tabstops.isEmpty ()) {
		x.writeStartElement ("tabstops");
		for (const auto &t : form.tabstops)
			x.writeTextElement ("tabstop", t);
		x.writeEndElement ();
	}
	x.writeEmptyElement ("resources");
	x.writeEmptyElement ("connections");
	x.writeEndElement ();
	x.writeEndDocument ();
	return out;
}

QStringList UiSetNames (const QString &set)
{
	QStringList out;
	for (QString p : set.split ('|', Qt::SkipEmptyParts)) {
		p = p.trimmed ();
		int k = p.lastIndexOf ("::");
		if (k >= 0) p = p.mid (k + 2);
		if (p == "AlignLeading") p = "AlignLeft";
		else if (p == "AlignTrailing") p = "AlignRight";
		if (!p.isEmpty ()) out.append (p);
	}
	return out;
}

}
