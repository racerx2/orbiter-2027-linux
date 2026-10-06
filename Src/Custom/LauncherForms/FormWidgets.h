
// custom: forms skins; widget classes the builder makes for forms (text fitting, buttons that don't toggle, flow layout, glow)

#ifndef __FORMS_FORMWIDGETS_H
#define __FORMS_FORMWIDGETS_H

#include <QCheckBox>
#include <QLabel>
#include <QLayout>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QTextLayout>
#include <QToolButton>
#include <map>
#include <vector>

namespace forms {

	// a QLabel that can wrap to a line count with a line height and elide, as QML Text; plain QLabel otherwise
	class FormLabel: public QLabel {
		Q_OBJECT
		Q_PROPERTY(int maxLines READ maxLines WRITE setMaxLines)
		Q_PROPERTY(double lineHeight READ lineHeight WRITE setLineHeight)
		Q_PROPERTY(bool elide READ elide WRITE setElide)
	public:
		explicit FormLabel (QWidget *parent = nullptr);
		int maxLines () const { return m_maxLines; }
		void setMaxLines (int n);
		double lineHeight () const { return m_lineHeight; }
		void setLineHeight (double f);
		bool elide () const { return m_elide; }
		void setElide (bool on);
		bool Fitted () const { return m_maxLines > 0 || m_lineHeight > 0.0 || m_elide; }
		QSize sizeHint () const override;
		QSize minimumSizeHint () const override;
		bool hasHeightForWidth () const override;
		int heightForWidth (int w) const override;

	protected:
		bool event (QEvent *e) override;
		void paintEvent (QPaintEvent *e) override;

	private:
		struct Fit { QStringList lines; double spacing = 0, height = 0, width = 0; };
		Fit Layout (int width) const;
		void Changed ();
		int m_maxLines = 0;
		double m_lineHeight = 0.0;
		bool m_elide = false;
		mutable int cacheWidth = -1;
		mutable QString cacheKey;
		mutable Fit cache;
	};

	// checkable buttons don't change state on a click: the form's action runs and the binding shows the state
	class FormButton: public QPushButton {
		Q_OBJECT
	public:
		explicit FormButton (QWidget *parent = nullptr): QPushButton (parent) { setAutoDefault (false); }
	protected:
		void nextCheckState () override {}
	};

	class FormToolButton: public QToolButton {
		Q_OBJECT
	public:
		explicit FormToolButton (QWidget *parent = nullptr): QToolButton (parent) {}
	protected:
		void nextCheckState () override {}
	};

	class FormCheckBox: public QCheckBox {
		Q_OBJECT
	public:
		explicit FormCheckBox (QWidget *parent = nullptr): QCheckBox (parent) {}
	protected:
		void nextCheckState () override {}
	};

	class FormRadioButton: public QRadioButton {
		Q_OBJECT
	public:
		explicit FormRadioButton (QWidget *parent = nullptr): QRadioButton (parent) {}
	protected:
		void nextCheckState () override {}
	};

	// a scroll area whose size hint is its content's (fitContent) instead of at most 24 lines
	class FormScrollArea: public QScrollArea {
		Q_OBJECT
	public:
		explicit FormScrollArea (QWidget *parent = nullptr): QScrollArea (parent) {}
		QSize sizeHint () const override;
	protected:
		bool eventFilter (QObject *o, QEvent *e) override; // QScrollArea already filters its widget
	};

	// items left to right, wrapping to new rows (Qt's flow layout example, height for width)
	class FlowLayout: public QLayout {
	public:
		explicit FlowLayout (QWidget *parent, int hSpacing, int vSpacing);
		~FlowLayout ();
		void addItem (QLayoutItem *item) override;
		void insertWidgetAt (int index, QWidget *w);
		int count () const override { return (int)items.size (); }
		QLayoutItem *itemAt (int index) const override;
		QLayoutItem *takeAt (int index) override;
		Qt::Orientations expandingDirections () const override { return {}; }
		bool hasHeightForWidth () const override { return true; }
		int heightForWidth (int width) const override;
		void setGeometry (const QRect &rect) override;
		QSize sizeHint () const override;
		QSize minimumSize () const override;

	private:
		int Place (const QRect &rect, bool apply) const;
		std::vector<QLayoutItem*> items;
		int hs, vs;
	};

	// rings around widgets, painted on the nearest ancestor that holds them (QML items don't clip)
	class GlowPainter: public QObject {
	public:
		explicit GlowPainter (QWidget *root);
		void Add (QWidget *target);
		void Changed (QWidget *target);  // its glow properties or state changed
		void Clear ();

	protected:
		bool eventFilter (QObject *obj, QEvent *e) override;

	private:
		struct Target {
			QPointer<QWidget> w;
			QPointer<QWidget> painter;   // the ancestor the rings are painted on
			QRect area;                  // last painted area, in the painter's coordinates
			std::vector<QPointer<QWidget>> watched; // the widget and those up to the painter
		};
		void Locate (Target &t);
		QRectF Rings (const Target &t, double *outset = nullptr) const;
		void Paint (QWidget *on, QPaintEvent *e);
		void Refresh (Target &t);
		QPointer<QWidget> root;
		std::vector<Target> targets;
		std::map<QObject*, int> filtered; // objects with this filter, use counts
		bool painting = false;
	};

	// QFont with only the letter spacing set: style sheet fonts keep it
	void SetLetterSpacing (QWidget *w, double px);

	// re-polish a widget and its descendants after a dynamic property used by style sheet selectors changed
	void Repolish (QWidget *w);

}

#endif // !__FORMS_FORMWIDGETS_H
