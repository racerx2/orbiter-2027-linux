
// custom: launcher layouts and forms skins; the subset of Qt Designer's .ui format they use (reader and writer)

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

	struct UiLimits {
		int maxBytes = UI_MAX_BYTES;
		int maxWidgets = UI_MAX_WIDGETS;
		int maxProperties = UI_MAX_PROPERTIES;
	};

	struct UiValue {
		enum Type { NONE, STRING, NUMBER, DOUBLE, BOOL, RECT, SIZE, ENUM, SET, FONT, PIXMAP,
			STRINGLIST, COLOR, ICON, CURSOR, SIZEPOLICY, URL };
		Type type = NONE;
		QString str;                 // STRING, ENUM, SET, PIXMAP, CURSOR (shape name or number), URL, ICON (theme)
		double num = 0.0;            // NUMBER, DOUBLE
		bool flag = false;           // BOOL
		QRect rect;                  // RECT; SIZE in its width and height
		QString family;              // FONT: "" if not given
		int pointSize = 0;           // FONT: 0 if not given
		int bold = -1, italic = -1;  // FONT: -1 if not given
		QStringList list;            // STRINGLIST; ICON: the eight states in UiIconStates order, "" if not given
		QString res;                 // PIXMAP, ICON: the qrc file Designer names, "" for a file path
		unsigned rgba = 0;           // COLOR: 0xAARRGGBB
		QString hPolicy, vPolicy;    // SIZEPOLICY: the names without "QSizePolicy::" or "Policy::"
		int hStretch = 0, vStretch = 0;

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
		static UiValue StringList (const QStringList &l);
		static UiValue Color (unsigned argb);
	};

	// iconset element names, in UiValue::list order
	extern const char *const UiIconStates[8];

	struct UiLayout;

	struct UiLayoutItem {
		enum Kind { WIDGET, LAYOUT, SPACER };
		Kind kind = WIDGET;
		int widget = -1;                                // WIDGET: index into the owner's children
		std::vector<UiLayout> sub;                      // LAYOUT: one entry
		int row = -1, col = -1, rowSpan = 1, colSpan = 1;
		QString alignment;                              // set text, "" if none
		QString name;                                   // SPACER
		std::vector<std::pair<QString, UiValue>> props; // SPACER: orientation, sizeType, sizeHint

		const UiValue *Prop (const QString &n) const;
	};

	struct UiLayout {
		QString cls, name;
		std::vector<std::pair<QString, UiValue>> props;
		QString stretch, rowStretch, colStretch, rowMinHeight, colMinWidth; // comma lists as written
		std::vector<UiLayoutItem> items;

		const UiValue *Prop (const QString &n) const;
	};

	struct UiWidget {
		QString cls, name;
		std::vector<std::pair<QString, UiValue>> props; // standard properties, file order
		std::vector<std::pair<QString, UiValue>> dyn;   // dynamic properties (stdset="0")
		std::vector<std::pair<QString, UiValue>> attrs; // <attribute> (a page's title)
		std::vector<UiWidget> children;                 // file order, also those inside the layout
		std::vector<UiLayout> layout;                   // the widget's layout: 0 or 1
		QStringList zorder;                             // <zorder> entries: raised in this order

		const UiValue *Prop (const QString &n) const;
		const UiValue *Dyn (const QString &n) const;
		const UiValue *Attr (const QString &n) const;
		void SetProp (const QString &n, const UiValue &v);
		void SetDyn (const QString &n, const UiValue &v);
		std::vector<const UiWidget*> PaintOrder () const; // children bottom to top
	};

	struct UiCustom {
		QString cls, extends;
		bool container = false;
	};

	struct UiForm {
		UiWidget root;
		QString cls;                    // <class>
		QStringList tabstops;
		std::vector<UiCustom> customs;  // <customwidgets>
		QStringList resources;          // <resources><include location>
		bool layout = false;            // a Designer layout somewhere in the form
		int widgets = 0;                // below the root

		const UiCustom *Custom (const QString &cls) const;
	};

	// tolerant of what Qt Designer 6 writes; false and err on malformed XML, caps or no widget
	bool ReadUiForm (const QByteArray &data, UiForm &form, QString &err, const UiLimits &lim = UiLimits ());
	bool ReadUiFile (const QString &path, UiForm &form, QString &err, const UiLimits &lim = UiLimits ());

	// in Designer's own layout; layouts are not written (the layouts' forms have none)
	QByteArray WriteUiForm (const UiForm &form);

	// enum and set names: last "::" part, Leading/Trailing as Left/Right
	QStringList UiSetNames (const QString &set);

}

#endif // !__CUSTOM_UIFORM_H
