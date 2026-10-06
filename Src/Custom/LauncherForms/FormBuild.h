
// custom: forms skins; from the .ui tree to widgets: classes, properties, layouts, pictures, anchors

#ifndef __FORMS_FORMBUILD_H
#define __FORMS_FORMBUILD_H

#include "UiForm.h"
#include <QIcon>
#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QWidget>
#include <functional>
#include <map>
#include <vector>

class QLayout;

namespace forms {

	using custom::UiForm;
	using custom::UiLayout;
	using custom::UiLayoutItem;
	using custom::UiValue;
	using custom::UiWidget;

	const int MAX_PICTURE = 4096;

	struct BuildEnv {
		QString formDir;                  // canonical folder of the form
		QString skinDir;                  // canonical skin folder
		std::map<QString, QString> qrc;   // ":/prefix/file" -> canonical file path
		std::function<void (const QString &)> warn;
		mutable QSet<QString> warned;     // pictures warned about once
		void Warn (const QString &s) const { if (warn) warn (s); }
	};

	bool ReadQrc (const QString &qrcFile, BuildEnv &env, QString &err);
	QString ResolveFile (const BuildEnv &env, const QString &name);      // canonical path inside the skin, "" if not
	QPixmap LoadPixmap (const BuildEnv &env, const QString &name);
	QIcon LoadIcon (const BuildEnv &env, const UiValue &v);
	QString RewriteStyle (const BuildEnv &env, const QString &qss);       // url() and ${SKIN} to absolute paths
	QString SafeText (const BuildEnv &env, const QString &richText);     // pictures and style sheets from the skin only
	QVariant SafeValue (const BuildEnv &env, QObject *w, const QByteArray &prop, const QVariant &v); // every Qt property set

	bool KnownClass (const QString &cls);
	bool PaintedClass (const QString &cls);
	QWidget *NewWidget (const QString &cls, QWidget *parent);

	QVariant NaturalValue (const BuildEnv &env, const UiValue &v);       // for dynamic properties
	bool SetStdProperty (const BuildEnv &env, QWidget *w, const QString &cls, const QString &name, const UiValue &v, QString &why);
	int AlignmentOf (const QString &set);

	// where the templates of a list container sit in its own layout
	struct TemplateSlot {
		int insertAt = -1;   // box and flow layouts: the item index of the first template
		int row = 0, col = 0;
		int align = 0;       // the template's alignment in the layout, for its clones
	};
	QLayout *MakeLayout (const BuildEnv &env, const UiLayout &l, QWidget *owner, const std::vector<QWidget*> &built,
		const std::vector<bool> &isTemplate, TemplateSlot &slot, bool top);

	// children of a widget without a layout that keep their distance to its edges
	class Anchors: public QObject {
		Q_OBJECT
	public:
		enum { LEFT = 1, RIGHT = 2, TOP = 4, BOTTOM = 8, HCENTER = 16, VCENTER = 32 };
		Anchors (QWidget *parent, const QSize &design);
		static int Parse (const QString &spec);
		void Add (QWidget *w, int flags, const QRect &design);
		void Apply ();
	protected:
		bool eventFilter (QObject *obj, QEvent *e) override;
	private:
		struct Item { QPointer<QWidget> w; int flags; QRect design; };
		QPointer<QWidget> parent;
		QSize design;
		std::vector<Item> items;
	};

	// the size a widget had in Designer: its geometry, or a fixed size; invalid if neither
	QSize DesignSize (const UiWidget &u);

}

#endif // !__FORMS_FORMBUILD_H
