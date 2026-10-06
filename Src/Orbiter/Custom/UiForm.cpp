
// custom: launcher layouts; the subset of Qt Designer's .ui format the layouts use (reader and writer)

#include "UiForm.h"
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>

namespace custom {

namespace {

	const int MAX_DEPTH = 64;

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

	// at <property>: its value element, then up to </property>
	void ReadProperty (QXmlStreamReader &x, UiValue &v)
	{
		bool have = false;
		while (x.readNextStartElement ()) {
			if (have) { x.skipCurrentElement (); continue; }
			have = true;
			const QStringView n = x.name ();
			if (n == u"string" || n == u"cstring") { v.type = UiValue::STRING; v.str = x.readElementText (QXmlStreamReader::SkipChildElements); }
			else if (n == u"number" || n == u"double" || n == u"float") {
				v.type = (n == u"number" ? UiValue::NUMBER : UiValue::DOUBLE);
				bool ok = false;
				double d = x.readElementText ().trimmed ().toDouble (&ok);
				v.num = (ok && std::isfinite (d) ? d : 0.0);
			}
			else if (n == u"bool") { v.type = UiValue::BOOL; v.flag = ToBool (x.readElementText ()); }
			else if (n == u"enum") { v.type = UiValue::ENUM; v.str = x.readElementText ().trimmed (); }
			else if (n == u"set") { v.type = UiValue::SET; v.str = x.readElementText ().trimmed (); }
			else if (n == u"pixmap") { v.type = UiValue::PIXMAP; v.str = x.readElementText (QXmlStreamReader::SkipChildElements).trimmed (); }
			else if (n == u"rect") { v.type = UiValue::RECT; ReadRect (x, v.rect); }
			else if (n == u"size") { v.type = UiValue::SIZE; ReadRect (x, v.rect); v.rect.moveTo (0, 0); }
			else if (n == u"font") { v.type = UiValue::FONT; ReadFont (x, v); }
			else x.skipCurrentElement ();
		}
	}

	bool ReadWidget (QXmlStreamReader &x, UiWidget &w, UiForm &form, int depth, QString &err)
	{
		w.cls = x.attributes ().value ("class").toString ();
		w.name = x.attributes ().value ("name").toString ();
		while (x.readNextStartElement ()) {
			const QStringView n = x.name ();
			if (n == u"property") {
				QString name = x.attributes ().value ("name").toString ();
				bool dyn = (x.attributes ().value ("stdset") == u"0");
				UiValue v;
				ReadProperty (x, v);
				if (w.props.size () + w.dyn.size () >= (size_t)UI_MAX_PROPERTIES) { err = QString ("more than %1 properties in one widget").arg (UI_MAX_PROPERTIES); return false; }
				if (!name.isEmpty ()) Put (dyn ? w.dyn : w.props, name, v);
			} else if (n == u"widget") {
				if (++form.widgets > UI_MAX_WIDGETS) { err = QString ("more than %1 widgets").arg (UI_MAX_WIDGETS); return false; }
				if (depth >= MAX_DEPTH) { err = "widgets nested too deep"; return false; }
				w.children.emplace_back ();
				if (!ReadWidget (x, w.children.back (), form, depth + 1, err)) return false;
			} else if (n == u"zorder") {
				w.zorder.append (x.readElementText ().trimmed ());
			} else {
				if (n == u"layout") form.layout = true;
				x.skipCurrentElement ();
			}
		}
		return !x.hasError ();
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
		case UiValue::PIXMAP: x.writeTextElement ("pixmap", v.str); break;
		case UiValue::FONT:
			x.writeStartElement ("font");
			if (!v.family.isEmpty ()) x.writeTextElement ("family", v.family);
			if (v.pointSize > 0) x.writeTextElement ("pointsize", QString::number (v.pointSize));
			if (v.bold >= 0) x.writeTextElement ("bold", v.bold ? "true" : "false");
			if (v.italic >= 0) x.writeTextElement ("italic", v.italic ? "true" : "false");
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

const UiValue *UiWidget::Prop (const QString &n) const { return Find (props, n); }
const UiValue *UiWidget::Dyn (const QString &n) const { return Find (dyn, n); }
void UiWidget::SetProp (const QString &n, const UiValue &v) { Put (props, n, v); }
void UiWidget::SetDyn (const QString &n, const UiValue &v) { Put (dyn, n, v); }

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

bool ReadUiForm (const QByteArray &data, UiForm &form, QString &err)
{
	form = UiForm ();
	if (data.size () > UI_MAX_BYTES) { err = "the file is larger than 1 MiB"; return false; }
	QXmlStreamReader x (data);
	bool haveRoot = false;
	if (!x.readNextStartElement () || x.name () != u"ui") {
		err = (x.hasError () ? x.errorString () : QString ("not a Qt Designer form (no <ui> element)"));
		return false;
	}
	while (x.readNextStartElement ()) {
		const QStringView n = x.name ();
		if (n == u"widget" && !haveRoot) {
			haveRoot = true;
			if (!ReadWidget (x, form.root, form, 0, err)) {
				if (err.isEmpty ()) err = x.errorString ();
				if (err.endsWith ('.')) err.chop (1);
				err = QString ("line %1: %2").arg (x.lineNumber ()).arg (err);
				return false;
			}
		} else if (n == u"tabstops") {
			while (x.readNextStartElement ()) {
				if (x.name () == u"tabstop") form.tabstops.append (x.readElementText ().trimmed ());
				else x.skipCurrentElement ();
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

bool ReadUiFile (const QString &path, UiForm &form, QString &err)
{
	if (!QFileInfo (path).isFile ()) { err = "is not a file"; return false; } // a FIFO would block
	QFile f (path);
	if (!f.open (QIODevice::ReadOnly)) { err = "can't be read"; return false; }
	if (f.size () > UI_MAX_BYTES) { err = "the file is larger than 1 MiB"; return false; }
	return ReadUiForm (f.read (UI_MAX_BYTES + 1), form, err);
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
