
// custom: launcher layouts; the subset of Qt Designer's .ui format the layouts use (reader and writer)

#ifndef __CUSTOM_UIFORM_H
#define __CUSTOM_UIFORM_H

#include <QByteArray>
#include <QRect>
#include <QString>
#include <QStringList>
#include <utility>
#include <vector>

namespace custom {

	const int UI_MAX_BYTES = 1 << 20;   // per file
	const int UI_MAX_WIDGETS = 1000;
	const int UI_MAX_PROPERTIES = 200; // per widget

	struct UiValue {
		enum Type { NONE, STRING, NUMBER, DOUBLE, BOOL, RECT, SIZE, ENUM, SET, FONT, PIXMAP };
		Type type = NONE;
		QString str;                 // STRING, ENUM, SET, PIXMAP
		double num = 0.0;            // NUMBER, DOUBLE
		bool flag = false;           // BOOL
		QRect rect;                  // RECT; SIZE in its width and height
		QString family;              // FONT: "" if not given
		int pointSize = 0;           // FONT: 0 if not given
		int bold = -1, italic = -1;  // FONT: -1 if not given

		int Int () const;
		static UiValue String (const QString &s);
		static UiValue Number (int n);
		static UiValue Double (double d);
		static UiValue Bool (bool b);
		static UiValue Rect (const QRect &r);
		static UiValue Size (int w, int h);
		static UiValue Enum (const QString &e);
		static UiValue Set (const QString &s);
		static UiValue Pixmap (const QString &path);
	};

	struct UiWidget {
		QString cls, name;
		std::vector<std::pair<QString, UiValue>> props; // standard properties, file order
		std::vector<std::pair<QString, UiValue>> dyn;   // dynamic properties (stdset="0")
		std::vector<UiWidget> children;                 // file order
		QStringList zorder;                             // <zorder> entries: raised in this order

		const UiValue *Prop (const QString &n) const;
		const UiValue *Dyn (const QString &n) const;
		void SetProp (const QString &n, const UiValue &v);
		void SetDyn (const QString &n, const UiValue &v);
		std::vector<const UiWidget*> PaintOrder () const; // children bottom to top
	};

	struct UiForm {
		UiWidget root;
		QStringList tabstops;
		bool layout = false;   // a Designer layout somewhere in the form
		int widgets = 0;       // below the root
	};

	// tolerant of what Qt Designer 6 writes; false and err on malformed XML, caps or no widget
	bool ReadUiForm (const QByteArray &data, UiForm &form, QString &err);
	bool ReadUiFile (const QString &path, UiForm &form, QString &err);

	// in Designer's own layout
	QByteArray WriteUiForm (const UiForm &form);

	// enum and set names: last "::" part, Leading/Trailing as Left/Right
	QStringList UiSetNames (const QString &set);

}

#endif // !__CUSTOM_UIFORM_H
