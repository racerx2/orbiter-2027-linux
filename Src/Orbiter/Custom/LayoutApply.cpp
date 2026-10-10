
// custom: launcher layouts; applies a Qt Designer form to a dialog built from a template, and exports one (Qt only)

#include "LayoutApply.h"
#include "OrbiterResource.h"
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSlider>
#include <QTextDocument>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <strings.h>

namespace custom {

namespace {

	const unsigned long S_WS_VISIBLE = 0x10000000, S_WS_CHILD = 0x40000000, S_DS_CENTER = 0x800;
	const int MIN_CTRL = 8, MIN_PAGEAREA = 100, MAX_PICTURE = 4096;
	const int COPY_MAX_FILES = 200;
	const qint64 COPY_MAX_BYTES = 64 << 20;

	// ---------------------------------------------------------------------------------------------
	// facts about the stock dialogs

	QString IdName (const RESCONTROL *c)
	{
		return (c->idname ? QString::fromUtf8 (c->idname) : QString ("id%1").arg (c->id)); // as CreateResDialog names it
	}

	bool SystemFace (const char *face)
	{
		static const char *sysfaces[] = {"MS Shell Dlg", "MS Shell Dlg 2", "MS Sans Serif", "Microsoft Sans Serif", "Tahoma", "Segoe UI", "System"};
		for (const char *f : sysfaces)
			if (face && !strcasecmp (face, f)) return true;
		return !face || !face[0];
	}

	bool Undeletable (const QString &dlg, const QString &id)
	{
		static const QStringList main = {"IDLAUNCH", "IDEXIT", "IDC_MNU_SCN", "IDC_MNU_OPT", "IDC_MNU_MOD", "IDC_MNU_VID",
			"IDC_MNU_EXT", "IDC_MNU_ABT", "IDC_MNU_PAGECONTAINER"};
		if (dlg == "IDD_MAIN") return main.contains (id);
		if (dlg == "IDD_PAGE_EXT") return id == "IDC_EXT_LIST" || id == "IDC_EXT_OPEN";
		if (dlg == "IDD_PAGE_OPT") return id == "IDC_OPT_PAGELIST" || id == "IDC_OPT_PAGECONTAINER";
		return false;
	}

	// controls whose visibility the classic code manages: an undo leaves them hidden
	bool CodeManaged (const QString &dlg, const RESCONTROL *c)
	{
		if (!(c->style & S_WS_VISIBLE)) return true;
		if (dlg == "IDD_PAGE_DEV" || dlg.startsWith ("IDD_OPTIONS_")) return true;
		const QString id = IdName (c);
		return dlg == "IDD_PAGE_SCN" && (id == "IDC_SCN_DESC" || id == "IDC_SCN_HTML" || id == "IDC_SCN_INFO");
	}

	QString Note (const QString &dlg, const RESCONTROL *c)
	{
		struct N { const char *dlg, *id, *note; };
		static const N notes[] = {
			{"IDD_MAIN", "IDLAUNCH", "Orbiter places this: its top follows the bottom edge and its height is Exit's; in a narrow window Launch, Help and Exit shrink into a row"},
			{"IDD_MAIN", "id9", "Orbiter places this: its top follows the bottom edge and it moves right with the width"},
			{"IDD_MAIN", "IDEXIT", "Orbiter places this: its top follows the bottom edge and it moves right with the width"},
			{"IDD_MAIN", "IDC_BLACKBOX", "Orbiter sets this text and colours; its width grows with the window"},
			{"IDD_MAIN", "IDC_LOGO", "Orbiter resizes this to the black box's height when their heights differ"},
			{"IDD_MAIN", "IDC_SHADOW", "Orbiter places this: the full window width"},
			{"IDD_MAIN", "IDC_MNU_PAGECONTAINER", "The page area: grows with the window"},
			{"IDD_MAIN", "IDC_VERSION", "Orbiter sets this text; it follows the bottom edge"},
			{"IDD_PAGE_SCN", "IDC_SCN_LIST", "Orbiter places this inside the splitter; the split is at its width"},
			{"IDD_PAGE_SCN", "IDC_SCN_HTML", "Orbiter places this inside the splitter and shows it or the plain description"},
			{"IDD_PAGE_SCN", "IDC_SCN_DESC", "Orbiter places this inside the splitter and shows it or the HTML description"},
			{"IDD_PAGE_SCN", "IDC_SCN_SAVE", "Orbiter places this: it follows the bottom edge"},
			{"IDD_PAGE_SCN", "IDC_SCN_DELQS", "Orbiter places this: it follows the bottom edge; width = Save's"},
			{"IDD_PAGE_SCN", "IDC_SCN_INFO", "Orbiter places this: at the description's right edge, following the bottom edge"},
			{"IDD_PAGE_SCN", "IDC_SCN_PAUSED", "Orbiter places this: it follows the right edge"},
			{"IDD_PAGE_MOD", "IDC_MOD_TREE", "Orbiter places this inside the splitter; the split is at its width"},
			{"IDD_PAGE_MOD", "IDC_MOD_INFO", "Orbiter sets this text and places it inside the splitter"},
			{"IDD_PAGE_MOD", "IDC_MOD_BUTTON1", "Orbiter places this: it follows the bottom edge"},
			{"IDD_PAGE_MOD", "IDC_MOD_BUTTON2", "Orbiter places this: it follows the bottom edge"},
			{"IDD_PAGE_MOD", "IDC_MOD_DEACTALL", "Orbiter places this: it follows the bottom edge"},
			{"IDD_PAGE_EXT", "IDC_EXT_LIST", "Orbiter places this inside the splitter; the split is at its width"},
			{"IDD_PAGE_EXT", "IDC_EXT_TEXT", "Orbiter sets this text and places it inside the splitter"},
			{"IDD_PAGE_EXT", "IDC_EXT_OPEN", "Orbiter places this: it follows the bottom edge"},
			{"IDD_PAGE_OPT", "IDC_OPT_PAGELIST", "Orbiter places this inside the split: 120 pixels wide"},
			{"IDD_PAGE_ABT", "IDC_ABT_TXT_NAME", "Orbiter sets this text"},
			{"IDD_PAGE_ABT", "IDC_ABT_TXT_BUILDDATE", "Orbiter sets this text"},
			{"IDD_PAGE_ABT", "IDC_ABT_TXT_CPR", "Orbiter sets this text"},
			{"IDD_PAGE_ABT", "IDC_ABT_TXT_WEBADDR", "Orbiter sets this text"},
			{"IDD_PAGE_ABT", "IDC_ABT_LBOX_COMPONENT", "Orbiter fills this list"},
		};
		const QString id = IdName (c);
		QString n;
		for (const N &e : notes)
			if (dlg == e.dlg && id == e.id) n = e.note;
		if (!(c->style & S_WS_VISIBLE)) n += QString (n.isEmpty () ? "" : "; ") + "hidden at start: Orbiter shows it when needed";
		return n;
	}

	// the most derived class Qt Designer knows; "" if none
	QString DesignerClass (const QWidget *w)
	{
		static const QStringList known = {"QTextBrowser", "QTextEdit", "QPlainTextEdit", "QLineEdit", "QComboBox", "QListWidget",
			"QTreeWidget", "QPushButton", "QCheckBox", "QRadioButton", "QGroupBox", "QScrollBar", "QSlider", "QProgressBar", "QLabel", "QFrame"};
		for (const QMetaObject *mo = w->metaObject (); mo; mo = mo->superClass ()) {
			QString n = QString::fromLatin1 (mo->className ());
			if (known.contains (n)) return n;
		}
		return QString ();
	}

	// controls that show nothing in Designer: a framed stand-in of which only the geometry is used
	bool IsStandIn (const RESCONTROL *c, const QWidget *w)
	{
		QString cls = DesignerClass (w);
		if (cls.isEmpty () || c->kind == RES_UPDOWN || c->kind == RES_TABCONTROL) return true;
		return cls == "QFrame" && static_cast<const QFrame*> (w)->frameShape () == QFrame::NoFrame;
	}

	QString StandInTip (const RESCONTROL *c)
	{
		const QString id = IdName (c);
		QString what = (id.contains ("SPLIT") ? "a splitter: Orbiter places its two panes inside it"
			: id.contains ("PAGECONTAINER") ? "the page area: Orbiter shows the pages in it"
			: c->kind == RES_UPDOWN ? "up and down arrows"
			: c->kind == RES_TABCONTROL ? "a tab strip"
			: "an area Orbiter draws itself");
		return id + ": " + what + ". Only its position and size are used.";
	}

	bool IsContainer (const RESCONTROL *c, const QWidget *w)
	{
		return c->kind == RES_GROUPBOX || (IsStandIn (c, w) && IdName (c).contains ("SPLIT"));
	}

	// the template's widgets by template index (CreateResDialog's direct children)
	std::vector<QWidget*> TemplateWidgets (QWidget *dlg, const RESDIALOG *d)
	{
		std::vector<QWidget*> ws (std::max (0, d->nctrl), nullptr);
		for (QObject *o : dlg->children ()) {
			QWidget *x = qobject_cast<QWidget*> (o);
			QVariant v = (x ? x->property ("resCtl") : QVariant ());
			if (!v.isValid ()) continue;
			const RESCONTROL *c = (const RESCONTROL*)v.value<void*> ();
			if (c >= d->ctrl && c < d->ctrl + d->nctrl) ws[c - d->ctrl] = x;
		}
		return ws;
	}

	std::vector<QWidget*> StockTabOrder (const RESDIALOG *d, const std::vector<QWidget*> &ws)
	{
		std::vector<QWidget*> out;
		for (int i = 0; i < d->nctrl; i++)
			if (ws[i] && !IsStandIn (d->ctrl + i, ws[i]) && (ws[i]->focusPolicy () & Qt::TabFocus)) out.push_back (ws[i]);
		return out;
	}

	bool ParsePair (const QString &s, int &idx, int &id)
	{
		QStringList p = s.trimmed ().split (':');
		bool a = false, b = false;
		if (p.size () == 2) idx = p[0].trimmed ().toInt (&a), id = p[1].trimmed ().toInt (&b);
		return a && b;
	}

	// a template control's kind, text, picture and style as 8 hex digits (FNV-1a), stored in the export as orbiterFp
	QString Fingerprint (const RESCONTROL *c)
	{
		const QByteArray b = QByteArray::number (c->kind) + '|' + (c->text ? QByteArray ("t") + c->text : QByteArray ("-")) + '|'
			+ QByteArray::number (c->imgid) + '|' + QByteArray::number ((qulonglong)c->style);
		quint32 h = 2166136261u;
		for (char ch : b) h = (h ^ (unsigned char)ch) * 16777619u;
		return QString ("%1").arg (h, 8, 16, QChar ('0'));
	}

	// what an "index:id" pair is resolved against: the template now and the export's orbiterControls (and orbiterFps)
	struct Pairs {
		const RESDIALOG *d;
		const std::vector<QWidget*> &ws;
		std::vector<QString> fp;                          // the template's fingerprints
		std::map<int, std::pair<int, QString>> listed;    // the export's list: index -> id, fingerprint
		bool listFps = false;                             // the export's list has fingerprints

		Pairs (const RESDIALOG *d, const std::vector<QWidget*> &ws, const UiWidget &root): d (d), ws (ws)
		{
			for (int i = 0; i < d->nctrl; i++) fp.push_back (Fingerprint (d->ctrl + i));
			const UiValue *ctl = root.Dyn ("orbiterControls"), *fps = root.Dyn ("orbiterFps");
			if (!ctl) return;
			const QStringList p = ctl->str.split (',', Qt::SkipEmptyParts);
			const QStringList f = (fps ? fps->str.split (',', Qt::SkipEmptyParts) : QStringList ());
			listFps = (f.size () == p.size ());
			for (int k = 0; k < p.size (); k++) {
				int i = -1, id = 0;
				if (ParsePair (p[k], i, id)) listed[i] = {id, listFps ? f[k].trimmed () : QString ()};
			}
		}

		// the export listed the same controls 0..idx as the template has now: nothing was added or removed before idx
		bool SamePrefix (int idx, bool withFp) const
		{
			if (withFp && !listFps) return false;
			for (int j = 0; j <= idx; j++) {
				auto it = listed.find (j);
				if ((it != listed.end ()) != (ws[j] != nullptr)) return false;
				if (it != listed.end () && (it->second.first != d->ctrl[j].id || (withFp && it->second.second != fp[j]))) return false;
			}
			return true;
		}

		// the template index the pair names now, -1 if none; fpIn "" for an export without fingerprints
		int Resolve (int idx, int id, const QString &fpIn) const
		{
			const QString f = fpIn.trimmed ();
			int n = 0, found = -1, nf = 0, foundF = -1;
			for (int i = 0; i < d->nctrl; i++) {
				if (d->ctrl[i].id != id) continue;
				n++, found = i;
				if (!f.isEmpty () && fp[i] == f) nf++, foundF = i;
			}
			if (idx >= 0 && idx < d->nctrl && d->ctrl[idx].id == id) {
				if (f.isEmpty () ? (n == 1 || SamePrefix (idx, false)) : (fp[idx] == f && (nf == 1 || SamePrefix (idx, true)))) return idx;
			}
			if (nf == 1) return foundF;
			return (n == 1 ? found : -1); // a unique id names its control even with another text; a shared one (IDC_STATIC) doesn't
		}
	};

	// ---------------------------------------------------------------------------------------------
	// property values

	QString AlignText (Qt::Alignment a)
	{
		Qt::Alignment h = a & Qt::AlignHorizontal_Mask, v = a & Qt::AlignVertical_Mask;
		QString hs = (h & Qt::AlignRight ? "Qt::AlignRight" : h & Qt::AlignHCenter ? "Qt::AlignHCenter" : h & Qt::AlignJustify ? "Qt::AlignJustify" : "Qt::AlignLeft");
		if (!v) return hs; // no vertical part: a line edit's own default
		QString vs = (v & Qt::AlignBottom ? "Qt::AlignBottom" : v & Qt::AlignVCenter ? "Qt::AlignVCenter" : "Qt::AlignTop");
		return hs + "|" + vs;
	}

	Qt::Alignment ParseAlign (const QString &s)
	{
		static const std::map<QString, Qt::Alignment> names = {
			{"AlignLeft", Qt::AlignLeft}, {"AlignRight", Qt::AlignRight}, {"AlignHCenter", Qt::AlignHCenter},
			{"AlignJustify", Qt::AlignJustify}, {"AlignTop", Qt::AlignTop}, {"AlignBottom", Qt::AlignBottom},
			{"AlignVCenter", Qt::AlignVCenter}, {"AlignCenter", Qt::AlignCenter}, {"AlignAbsolute", Qt::AlignAbsolute},
			{"AlignBaseline", Qt::AlignBaseline}};
		Qt::Alignment a;
		for (const QString &n : UiSetNames (s))
			if (auto it = names.find (n); it != names.end ()) a |= it->second;
		return a;
	}

	bool ParseFrame (const QString &s, QFrame::Shape &shape)
	{
		static const std::map<QString, QFrame::Shape> names = {{"NoFrame", QFrame::NoFrame}, {"Box", QFrame::Box},
			{"Panel", QFrame::Panel}, {"StyledPanel", QFrame::StyledPanel}, {"HLine", QFrame::HLine}, {"VLine", QFrame::VLine},
			{"WinPanel", QFrame::WinPanel}};
		QStringList n = UiSetNames (s);
		auto it = (n.isEmpty () ? names.end () : names.find (n.last ()));
		if (it == names.end ()) return false;
		shape = it->second;
		return true;
	}

	bool ParseShadow (const QString &s, QFrame::Shadow &shadow)
	{
		QStringList n = UiSetNames (s);
		QString x = (n.isEmpty () ? QString () : n.last ());
		if (x == "Plain") shadow = QFrame::Plain;
		else if (x == "Raised") shadow = QFrame::Raised;
		else if (x == "Sunken") shadow = QFrame::Sunken;
		else return false;
		return true;
	}

	const UiValue *Typed (const UiWidget &u, const char *name, UiValue::Type t)
	{
		const UiValue *v = u.Prop (name);
		return (v && v->type == t ? v : nullptr);
	}

	const UiValue *Text (const UiWidget &u, const char *name)
	{
		return Typed (u, name, UiValue::STRING);
	}

	const UiValue *Flags (const UiWidget &u, const char *name)
	{
		const UiValue *v = u.Prop (name);
		return (v && (v->type == UiValue::SET || v->type == UiValue::ENUM) ? v : nullptr);
	}

	bool Align (const UiWidget &u, Qt::Alignment &a)
	{
		const UiValue *v = Flags (u, "alignment");
		if (!v) return false;
		a = ParseAlign (v->str);
		return a.toInt () != 0;
	}

	// only what the form gives and differs; true if f changed
	bool MergeFont (QFont &f, const UiValue &v)
	{
		bool ch = false;
		if (!v.family.isEmpty () && v.family != f.family ()) f.setFamily (v.family), ch = true;
		if (v.pointSize > 0 && v.pointSize <= 200 && v.pointSize != f.pointSize ()) f.setPointSize (v.pointSize), ch = true;
		if (v.bold >= 0 && (v.bold != 0) != f.bold ()) f.setBold (v.bold != 0), ch = true;
		if (v.italic >= 0 && (v.italic != 0) != f.italic ()) f.setItalic (v.italic != 0), ch = true;
		return ch;
	}

	QString SubstSkin (QString text, const QString &skinDir)
	{
		QString dir = skinDir;
		dir.replace ("\\", "\\\\");
		dir.replace ("\"", "\\\"");
		text.replace ("${SKIN}", dir);
		return text;
	}

	// a picture file inside the skin folder, relative to the .ui file
	bool LoadPicture (const QString &rel, const QString &uiDir, const QString &skinDir, QImage &img, QString &err)
	{
		if (rel.isEmpty () || rel.startsWith (':') || rel.startsWith ("qrc:", Qt::CaseInsensitive)) {
			err = "is not a file in the skin folder";
			return false;
		}
		QString f = QFileInfo (QDir (uiDir), rel).canonicalFilePath ();
		QString root = QDir (skinDir).canonicalPath ();
		if (f.isEmpty ()) { err = "was not found"; return false; }
		if (root.isEmpty () || !f.startsWith (root + '/')) { err = "is outside the skin folder"; return false; }
		if (!QFileInfo (f).isFile ()) { err = "is not a file"; return false; }
		QImageReader rd (f);
		QSize sz = rd.size ();
		if (sz.isValid () && (sz.width () > MAX_PICTURE || sz.height () > MAX_PICTURE)) { err = "is larger than 4096 x 4096"; return false; }
		img = rd.read ();
		if (img.isNull ()) { err = "can't be read: " + rd.errorString (); return false; }
		if (img.width () > MAX_PICTURE || img.height () > MAX_PICTURE) { err = "is larger than 4096 x 4096"; img = QImage (); return false; }
		return true;
	}

	// ---------------------------------------------------------------------------------------------
	// what the layouts changed, for the undo; never destroyed (it holds fonts and images past QApplication)

	struct Change {
		enum Kind { TEXT, TITLE, TOOLTIP, PIXMAP, FONT, STYLE } kind;
		QPointer<QWidget> w;
		QString before, set;
		Qt::TextFormat format = Qt::AutoText;
		QImage picture;
		qint64 pixmapKey = 0;
		QFont font;
		bool hadFont = false;
	};

	struct Hidden {
		QPointer<QWidget> w;
		bool managed;
	};

	struct State {
		std::vector<Change> changes;
		std::vector<QPointer<QWidget>> decorations;
		std::vector<Hidden> hidden;
		std::vector<QPointer<QObject>> filters;
		std::vector<QPointer<QWidget>> viewHidden;
		bool undone = false;
	};

	State &S ()
	{
		static State *s = new State;
		return *s;
	}

	void Prune ()
	{
		State &s = S ();
		std::erase_if (s.changes, [](const Change &c) { return c.w.isNull (); });
		std::erase_if (s.decorations, [](const QPointer<QWidget> &w) { return w.isNull (); });
		std::erase_if (s.hidden, [](const Hidden &h) { return h.w.isNull (); });
		std::erase_if (s.filters, [](const QPointer<QObject> &f) { return f.isNull (); });
		std::erase_if (s.viewHidden, [](const QPointer<QWidget> &w) { return w.isNull (); });
	}

	// removed controls stay hidden: a show by the classic code is undone at once (as ClassicHider does)
	class StayHidden: public QObject {
	public:
		StayHidden (QObject *parent): QObject (parent) {}
		void Add (QWidget *w) { w->installEventFilter (this); w->hide (); }
	protected:
		bool eventFilter (QObject *obj, QEvent *event) override
		{
			if (event->type () == QEvent::ShowToParent) {
				QPointer<QWidget> w = static_cast<QWidget*> (obj);
				QMetaObject::invokeMethod (this, [w]() { if (w) w->hide (); }, Qt::QueuedConnection);
			}
			return false;
		}
	};

	// decorations that keep their distance from the right or bottom edge
	class Anchors: public QObject {
	public:
		struct A { QPointer<QWidget> w; bool right, bottom; int dr, db; };
		Anchors (QWidget *dlg): QObject (dlg), dlg (dlg) { dlg->installEventFilter (this); }
		void Add (const A &a) { list.push_back (a); }
	protected:
		bool eventFilter (QObject *obj, QEvent *event) override
		{
			if (obj == dlg && event->type () == QEvent::Resize)
				for (const A &a : list) {
					if (!a.w) continue;
					QRect g = a.w->geometry ();
					a.w->move (a.right ? dlg->width () - a.dr - g.width () : g.x (), a.bottom ? dlg->height () - a.db - g.height () : g.y ());
				}
			return false;
		}
	private:
		QWidget *dlg;
		std::vector<A> list;
	};

	void Record (Change c)
	{
		S ().changes.push_back (std::move (c));
	}

	void SetText (QWidget *w, Change::Kind kind, const QString &before, const QString &text)
	{
		Change c;
		c.kind = kind;
		c.w = w;
		c.before = before;
		c.set = text;
		if (QLabel *l = qobject_cast<QLabel*> (w)) {
			c.format = l->textFormat ();
			c.picture = l->pixmap ().toImage ();
			if (Qt::mightBeRichText (text)) l->setTextFormat (Qt::PlainText); // texts from a layout are shown as typed
			l->setText (text);
		} else if (QAbstractButton *b = qobject_cast<QAbstractButton*> (w)) b->setText (text);
		else if (QGroupBox *g = qobject_cast<QGroupBox*> (w)) g->setTitle (text);
		else return;
		Record (c);
	}

	void SetPicture (QLabel *l, const QImage &img)
	{
		Change c;
		c.kind = Change::PIXMAP;
		c.w = l;
		c.before = l->text ();
		c.format = l->textFormat ();
		c.picture = l->pixmap ().toImage ();
		l->setPixmap (QPixmap::fromImage (img));
		c.pixmapKey = l->pixmap ().cacheKey ();
		Record (c);
	}

	// a font for a widget and for the widgets inside it that have its font (CreateResDialog sets it on each)
	void SetFont (QWidget *w, const QFont &f)
	{
		const QFont old = w->font ();
		if (f == old) return;
		std::vector<QWidget*> targets = {w};
		for (QWidget *x : w->findChildren<QWidget*> ())
			if (!x->isWindow () && x->font () == old) targets.push_back (x);
		for (QWidget *x : targets) {
			Change c;
			c.kind = Change::FONT;
			c.w = x;
			c.font = x->font ();
			c.hadFont = x->testAttribute (Qt::WA_SetFont);
			x->setFont (f);
			Record (c);
		}
	}

	void RestoreLabel (QLabel *l, const Change &c)
	{
		if (!c.picture.isNull ()) l->setPixmap (QPixmap::fromImage (c.picture));
		else {
			l->setTextFormat (c.format);
			l->setText (c.before);
		}
	}

	void SetStyle (QWidget *w, const QString &style)
	{
		if (style == w->styleSheet ()) return;
		Change c;
		c.kind = Change::STYLE;
		c.w = w;
		c.before = w->styleSheet ();
		w->setStyleSheet (style);
		Record (c);
	}

	void Revert (const Change &c)
	{
		QWidget *w = c.w;
		if (!w) return;
		switch (c.kind) {
		case Change::TEXT:
		case Change::TITLE:
			if (QLabel *l = qobject_cast<QLabel*> (w)) {
				if (l->text () == c.set && l->pixmap ().isNull ()) RestoreLabel (l, c);
			} else if (QAbstractButton *b = qobject_cast<QAbstractButton*> (w)) {
				if (b->text () == c.set) b->setText (c.before);
			} else if (QGroupBox *g = qobject_cast<QGroupBox*> (w)) {
				if (g->title () == c.set) g->setTitle (c.before);
			}
			break;
		case Change::TOOLTIP:
			if (w->toolTip () == c.set) w->setToolTip (c.before);
			break;
		case Change::PIXMAP:
			if (QLabel *l = qobject_cast<QLabel*> (w); l && l->pixmap ().cacheKey () == c.pixmapKey) {
				if (c.picture.isNull ()) l->setPixmap (QPixmap ());
				RestoreLabel (l, c);
			}
			break;
		case Change::FONT:
			w->setFont (c.hadFont ? c.font : QFont ()); // a default font follows the parent again
			break;
		case Change::STYLE:
			w->setStyleSheet (c.before);
			break;
		}
	}

	// ---------------------------------------------------------------------------------------------
	// applying

	// the properties of a stock control a layout may change
	void ApplyProps (QWidget *w, const UiWidget &u, const QString &skinDir, const QString &uiDir, const LayoutLog &say, const QString &id)
	{
		const UiValue *v;
		Qt::Alignment a;
		if (QLabel *l = qobject_cast<QLabel*> (w)) {
			if ((v = Typed (u, "pixmap", UiValue::PIXMAP))) {
				QImage img;
				QString err;
				if (LoadPicture (v->str, uiDir, skinDir, img, err)) SetPicture (l, img);
				else say (id + ": the picture " + v->str + " " + err);
			} else if ((v = Text (u, "text")) && v->str != l->text ()) SetText (l, Change::TEXT, l->text (), v->str);
			if (Align (u, a) && a != l->alignment ()) l->setAlignment (a);
			if ((v = Typed (u, "wordWrap", UiValue::BOOL)) && v->flag != l->wordWrap ()) l->setWordWrap (v->flag);
		} else if (QAbstractButton *b = qobject_cast<QAbstractButton*> (w)) {
			if ((v = Text (u, "text")) && v->str != b->text ()) SetText (b, Change::TEXT, b->text (), v->str);
			if (QPushButton *p = qobject_cast<QPushButton*> (w); p && (v = Typed (u, "flat", UiValue::BOOL)) && v->flag != p->isFlat ()) p->setFlat (v->flag);
		} else if (QGroupBox *g = qobject_cast<QGroupBox*> (w)) {
			if ((v = Text (u, "title")) && v->str != g->title ()) SetText (g, Change::TITLE, g->title (), v->str);
			if ((v = Typed (u, "flat", UiValue::BOOL)) && v->flag != g->isFlat ()) g->setFlat (v->flag);
		} else if (QLineEdit *e = qobject_cast<QLineEdit*> (w)) {
			if (Align (u, a) && a != e->alignment ()) e->setAlignment (a);
		}
		if ((v = Text (u, "toolTip")) && v->str != w->toolTip ()) {
			Change c;
			c.kind = Change::TOOLTIP;
			c.w = w;
			c.before = w->toolTip ();
			c.set = v->str;
			w->setToolTip (v->str);
			Record (c);
		}
		if ((v = Typed (u, "font", UiValue::FONT))) {
			QFont f = w->font ();
			if (MergeFont (f, *v)) SetFont (w, f);
		}
		if ((v = Text (u, "styleSheet"))) SetStyle (w, SubstSkin (v->str, skinDir));
	}

	// a widget added in Designer; nullptr for classes that would do nothing
	QWidget *MakeDecoration (QWidget *dlg, const UiWidget &u, const QString &skinDir, const QString &uiDir, const LayoutLog &say)
	{
		const UiValue *v;
		Qt::Alignment a;
		QWidget *w = nullptr;
		QFrame *frame = nullptr;
		if (u.cls == "QLabel") {
			QLabel *l = new QLabel (dlg);
			if ((v = Typed (u, "pixmap", UiValue::PIXMAP))) {
				QImage img;
				QString err;
				if (LoadPicture (v->str, uiDir, skinDir, img, err)) l->setPixmap (QPixmap::fromImage (img));
				else say (u.name + ": the picture " + v->str + " " + err);
			} else if ((v = Text (u, "text"))) {
				l->setTextFormat (Qt::PlainText);
				l->setText (v->str);
			}
			if ((v = Typed (u, "scaledContents", UiValue::BOOL))) l->setScaledContents (v->flag);
			if (Align (u, a)) l->setAlignment (a);
			if ((v = Typed (u, "wordWrap", UiValue::BOOL))) l->setWordWrap (v->flag);
			w = frame = l;
		} else if (u.cls == "QFrame") {
			w = frame = new QFrame (dlg);
		} else if (u.cls == "Line") {
			frame = new QFrame (dlg);
			const UiValue *o = Flags (u, "orientation");
			frame->setFrameShape (o && o->str.endsWith ("Vertical") ? QFrame::VLine : QFrame::HLine);
			frame->setFrameShadow (QFrame::Sunken);
			w = frame;
		} else if (u.cls == "QGroupBox") {
			QGroupBox *g = new QGroupBox (dlg);
			if ((v = Text (u, "title"))) g->setTitle (v->str);
			if ((v = Typed (u, "flat", UiValue::BOOL))) g->setFlat (v->flag);
			w = g;
		}
		if (!w) return nullptr;
		if (frame) {
			QFrame::Shape shape;
			QFrame::Shadow shadow;
			if ((v = Flags (u, "frameShape")) && ParseFrame (v->str, shape)) frame->setFrameShape (shape);
			if ((v = Flags (u, "frameShadow")) && ParseShadow (v->str, shadow)) frame->setFrameShadow (shadow);
			if ((v = Typed (u, "lineWidth", UiValue::NUMBER))) frame->setLineWidth (std::clamp (v->Int (), 0, 64));
		}
		w->setObjectName (u.name);
		w->setProperty ("orbiterDecoration", true);
		w->setAttribute (Qt::WA_TransparentForMouseEvents);
		w->setFocusPolicy (Qt::NoFocus);
		QFont f = dlg->font ();
		if ((v = Typed (u, "font", UiValue::FONT))) MergeFont (f, *v);
		w->setFont (f);
		if ((v = Text (u, "styleSheet"))) w->setStyleSheet (SubstSkin (v->str, skinDir));
		return w;
	}

}

const QStringList &LayoutDialogs ()
{
	static const QStringList names = {
		"IDD_MAIN", "IDD_PAGE_SCN", "IDD_PAGE_OPT", "IDD_PAGE_MOD", "IDD_PAGE_DEV", "IDD_PAGE_EXT", "IDD_PAGE_ABT", "IDD_PAGE_WAIT2",
		"IDD_OPTIONS_VISUAL", "IDD_OPTIONS_PHYSICS", "IDD_OPTIONS_INSTRUMENT", "IDD_OPTIONS_VESSEL", "IDD_OPTIONS_UI",
		"IDD_OPTIONS_JOYSTICK", "IDD_OPTIONS_CELSPHERE", "IDD_OPTIONS_VISHELPER", "IDD_OPTIONS_PLANETARIUM", "IDD_OPTIONS_LABELS",
		"IDD_OPTIONS_BODYFORCE", "IDD_OPTIONS_FRAMEAXES",
		"IDD_EXTRA_DYNAMICS", "IDD_EXTRA_ADYNAMICS", "IDD_EXTRA_STABILISATION", "IDD_EXTRA_MFDCONFIG", "IDD_EXTRA_SHUTDOWN",
		"IDD_EXTRA_FIXEDSTEP", "IDD_EXTRA_DBGRENDER", "IDD_EXTRA_PERFORMANCE", "IDD_EXTRA_LAUNCHPAD", "IDD_EXTRA_LOGFILE",
		"IDD_SAVESCN", "IDD_MSG"};
	return names;
}

bool IsLayoutDialog (const QString &name)
{
	return LayoutDialogs ().contains (name);
}

QString CheckLayoutForm (const UiForm &form, const QString &dialog)
{
	if (form.layout) return "it uses a Qt Designer layout; this version needs absolute positions (Form > Break Layout)";
	const UiValue *g = form.root.Prop ("geometry");
	if (dialog == "IDD_MAIN" && g && g->type == UiValue::RECT && (g->rect.width () < LAYOUT_MINW || g->rect.height () < LAYOUT_MINH))
		return QString ("the form is %1 x %2, smaller than the Launchpad's minimum of %3 x %4").arg (g->rect.width ()).arg (g->rect.height ()).arg (LAYOUT_MINW).arg (LAYOUT_MINH);
	return QString ();
}

bool ApplyLayout (QWidget *dlg, const RESDIALOG *d, const UiForm &form, const QString &skinDir,
	const QString &uiFile, const LayoutLog &log, LayoutInfo &info, QString &err)
{
	const QString dname = QString::fromUtf8 (d->name);
	err = CheckLayoutForm (form, dname);
	if (!err.isEmpty ()) return false;
	if (S ().undone) { err = "the layout was undone"; return false; }
	Prune ();
	LayoutLog say = [&log, &dname](const QString &s) { if (log) log (dname + ": " + s); };
	const QString uiDir = QFileInfo (uiFile).absolutePath ();
	const std::vector<QWidget*> ws = TemplateWidgets (dlg, d);
	const UiWidget &root = form.root;
	const Pairs pairs (d, ws, root);
	const bool popup = !(d->style & S_WS_CHILD);

	// the user's base units against those of the export
	double sx = 1.0, sy = 1.0;
	if (const UiValue *v = root.Dyn ("orbiterBaseX"); v && v->num > 0.5) sx = std::clamp (dlg->property ("resBaseX").toDouble () / v->num, 0.25, 4.0);
	if (const UiValue *v = root.Dyn ("orbiterBaseY"); v && v->num > 0.5) sy = std::clamp (dlg->property ("resBaseY").toDouble () / v->num, 0.25, 4.0);
	auto scale = [sx, sy](const QRect &r) {
		return QRect ((int)std::lround (r.x () * sx), (int)std::lround (r.y () * sy), (int)std::lround (r.width () * sx), (int)std::lround (r.height () * sy));
	};
	QSize ref = dlg->size ();
	if (const UiValue *g = Typed (root, "geometry", UiValue::RECT)) ref = scale (g->rect).size ();
	if (dname == "IDD_MAIN") ref = ref.expandedTo (QSize (LAYOUT_MINW, LAYOUT_MINH));
	ref = ref.expandedTo (QSize (40, 30)).boundedTo (QSize (16384, 16384));
	if (const UiValue *v = Typed (root, "font", UiValue::FONT)) { // the form's font reaches every control without one of its own
		QFont f = dlg->font ();
		if (MergeFont (f, *v)) SetFont (dlg, f);
	}

	// the form depth first in paint order, with rectangles in dialog coordinates
	struct Item { const UiWidget *u; QRect r; int idx; QWidget *w; }; // idx: template index, -1 decoration, -2 skipped
	std::vector<Item> items;
	std::vector<int> owner (ws.size (), -1);
	std::function<void (const UiWidget &, QPoint)> walk = [&](const UiWidget &p, QPoint off) {
		for (const UiWidget *c : p.PaintOrder ()) {
			const UiValue *g = Typed (*c, "geometry", UiValue::RECT);
			QRect a = (g ? g->rect : QRect ()).translated (off);
			int idx = -1;
			if (const UiValue *v = c->Dyn ("orbiterCtl")) {
				int i = -1, id = 0;
				const UiValue *f = c->Dyn ("orbiterFp");
				idx = (ParsePair (v->str, i, id) ? pairs.Resolve (i, id, f ? f->str : QString ()) : -1);
				if (idx < 0) {
					say (c->name + ": no control " + v->str + " in this Orbiter, or Orbiter moved it; skipped");
					idx = -2;
				} else if (owner[idx] >= 0) {
					say (c->name + ": a second copy of " + IdName (d->ctrl + idx) + "; skipped");
					idx = -2;
				}
			}
			items.push_back ({c, a, idx, idx >= 0 ? ws[idx] : nullptr});
			if (idx >= 0) owner[idx] = (int)items.size () - 1;
			walk (*c, a.topLeft ());
		}
	};
	walk (root, QPoint (0, 0));

	std::vector<char> listed (ws.size (), 0);
	const UiValue *ctlList = root.Dyn ("orbiterControls");
	for (const auto &[i, e] : pairs.listed) {
		int k = pairs.Resolve (i, e.first, e.second);
		if (k >= 0) listed[k] = 1;
	}

	// the stock controls: geometry, then what they show
	const QRect area (QPoint (0, 0), ref);
	for (const Item &it : items) {
		if (it.idx < 0 || !ws[it.idx]) continue;
		const RESCONTROL *c = d->ctrl + it.idx;
		QWidget *w = ws[it.idx];
		const QString id = IdName (c);
		QRect r = scale (it.r);
		if (r != w->geometry ()) {
			int mn = (id.contains ("PAGECONTAINER") ? MIN_PAGEAREA : MIN_CTRL);
			if (!area.contains (r) || r.width () < mn || r.height () < mn)
				say (id + QString (": the new place (%1, %2, %3 x %4) is off the form or smaller than %5 x %5; kept stock").arg (r.x ()).arg (r.y ()).arg (r.width ()).arg (r.height ()).arg (mn));
			else w->setGeometry (r);
		}
		if (!IsStandIn (c, w)) ApplyProps (w, *it.u, skinDir, uiDir, say, id);
	}

	// stock controls missing from the form: removed, unless listed as kept or not known to the export
	StayHidden *stay = nullptr;
	for (size_t i = 0; i < ws.size (); i++) {
		if (!ws[i] || owner[i] >= 0) continue;
		const RESCONTROL *c = d->ctrl + i;
		const QString id = IdName (c);
		if (!listed[i]) {
			if (ctlList) say (id + ": not in this layout (added to Orbiter after it was made); stays stock");
			continue;
		}
		if (Undeletable (dname, id)) {
			say (id + ": can't be removed; kept");
			continue;
		}
		if (!stay) {
			stay = new StayHidden (dlg);
			S ().filters.push_back (stay);
		}
		stay->Add (ws[i]);
		S ().hidden.push_back ({ws[i], CodeManaged (dname, c)});
	}

	// added widgets
	Anchors *anchors = nullptr;
	for (size_t k = 0; k < items.size (); k++) {
		Item &it = items[k];
		if (it.idx != -1) continue;
		QWidget *deco = MakeDecoration (dlg, *it.u, skinDir, uiDir, say);
		if (!deco) {
			say (it.u->name + ": a " + it.u->cls + " added in Designer would do nothing in Orbiter; left out");
			continue;
		}
		QRect r = scale (it.r);
		deco->setGeometry (r);
		S ().decorations.push_back (deco);
		it.w = deco;
		if (const UiValue *a = it.u->Dyn ("anchor"); a && a->type == UiValue::STRING) {
			bool right = a->str.contains ("right", Qt::CaseInsensitive), bottom = a->str.contains ("bottom", Qt::CaseInsensitive);
			if (right || bottom) {
				if (!anchors) {
					anchors = new Anchors (dlg);
					S ().filters.push_back (anchors);
				}
				anchors->Add ({deco, right, bottom, ref.width () - r.x () - r.width (), ref.height () - r.y () - r.height ()});
			}
		}
	}

	// stacking as Designer paints the form; controls the form doesn't know keep Orbiter's rule (group boxes under, the rest on top)
	for (const Item &it : items)
		if (it.w) it.w->raise ();
	for (size_t i = 0; i < ws.size (); i++)
		if (ws[i] && owner[i] < 0) {
			if (d->ctrl[i].kind == RES_GROUPBOX) ws[i]->lower ();
			else ws[i]->raise ();
		}

	// tab order, when the form's differs from Orbiter's
	if (form.tabstops.size () >= 2) {
		std::map<QString, QWidget*> byName;
		for (const Item &it : items)
			if (it.idx >= 0 && ws[it.idx] && !IsStandIn (d->ctrl + it.idx, ws[it.idx])) byName[it.u->name] = ws[it.idx];
		std::vector<QWidget*> order;
		for (const QString &n : form.tabstops)
			if (auto f = byName.find (n); f != byName.end () && std::find (order.begin (), order.end (), f->second) == order.end ()) order.push_back (f->second);
		std::vector<QWidget*> stock = StockTabOrder (d, ws);
		if (order.size () >= 2 && order != stock)
			for (size_t k = 1; k < order.size (); k++) QWidget::setTabOrder (order[k-1], order[k]);
	}

	// the form itself
	info = LayoutInfo ();
	if (const UiValue *v = Text (root, "windowTitle"); v && popup && !v->str.trimmed ().isEmpty () && v->str != dlg->windowTitle ())
		dlg->setWindowTitle (v->str); // the host keeps the main window's title for its hints
	if (const UiValue *v = Text (root, "styleSheet")) {
		if (dname == "IDD_MAIN") {
			info.rootStyle = SubstSkin (v->str, skinDir);
			dlg->setStyleSheet (info.rootStyle); // the host composes it with a skin's style sheet
		} else SetStyle (dlg, SubstSkin (v->str, skinDir));
	}
	if (dname == "IDD_MAIN") {
		const UiValue *v;
		int minh = LAYOUT_MINH, minw = LAYOUT_MINW;
		if ((v = Typed (root, "minimumSize", UiValue::SIZE))) { // pixels on the screen, not scaled
			minh = std::max (minh, v->rect.height ());
			minw = std::max (minw, v->rect.width ());
		}
		QWidget *launch = nullptr, *help = nullptr, *exit = nullptr;
		for (int i = 0; i < d->nctrl; i++) {
			const QString id = IdName (d->ctrl + i);
			if (id == "IDLAUNCH") launch = ws[i];
			else if (id == "id9") help = ws[i];
			else if (id == "IDEXIT") exit = ws[i];
		}
		if (launch && help && exit) { // the width below which LaunchpadDialog::Resize shrinks the buttons into a row
			QRect l = launch->geometry (), h = help->geometry (), e = exit->geometry ();
			int span = e.x () + e.width () - l.x (), bg = e.x () - (h.x () + h.width ()), ww = l.width () + h.width () + e.width ();
			minw = std::max (minw, ref.width () + ww + 2*bg - span);
		}
		info.minSize = QSize (std::min (minw, 16384), std::min (minh, 16384));
	}
	const QPoint centre = dlg->geometry ().center ();
	if (popup && dlg->minimumSize () == dlg->maximumSize () && dname != "IDD_MAIN") dlg->setFixedSize (ref);
	else dlg->resize (ref);
	if (popup && (d->style & S_DS_CENTER)) dlg->move (centre - QPoint (ref.width () / 2, ref.height () / 2));
	info.refSize = ref;
	return true;
}

UiForm ExportLayout (QWidget *dlg, const RESDIALOG *d, void *hModule, std::vector<LayoutImage> &images)
{
	const QString dname = QString::fromUtf8 (d->name);
	const std::vector<QWidget*> ws = TemplateWidgets (dlg, d);
	const int n = (int)ws.size ();
	UiForm form;
	UiWidget &root = form.root;
	root.cls = (!(d->style & S_WS_CHILD) ? "QDialog" : "QWidget");
	root.name = dname;
	root.SetProp ("geometry", UiValue::Rect (QRect (0, 0, dlg->width (), dlg->height ())));
	if (dname == "IDD_MAIN") root.SetProp ("minimumSize", UiValue::Size (LAYOUT_MINW, LAYOUT_MINH));
	UiValue font;
	font.type = UiValue::FONT;
	font.pointSize = std::max (1, d->fontsize);
	font.bold = (d->weight >= 700);
	font.italic = (d->italic != 0);
	if (!SystemFace (d->font)) font.family = QString::fromUtf8 (d->font);
	root.SetProp ("font", font);
	if (d->caption && d->caption[0]) root.SetProp ("windowTitle", UiValue::String (QString::fromUtf8 (d->caption)));

	// unique names that are C++ identifiers (uic needs them)
	std::vector<QString> names (n);
	QStringList used = {dname};
	for (int i = 0; i < n; i++) {
		if (!ws[i]) continue;
		QString b = IdName (d->ctrl + i);
		for (QChar &ch : b)
			if (!ch.isLetterOrNumber () && ch != '_') ch = '_';
		if (b.isEmpty () || b[0].isDigit ()) b.prepend ("id");
		QString s = b;
		for (int k = 2; used.contains (s); k++) s = b + "_" + QString::number (k);
		used.append (s);
		names[i] = s;
	}

	// nesting: the smallest group box or splitter that holds the control's centre
	std::vector<QRect> r (n);
	std::vector<char> cont (n, 0);
	std::vector<int> parent (n, -1);
	for (int i = 0; i < n; i++)
		if (ws[i]) r[i] = ws[i]->geometry (), cont[i] = IsContainer (d->ctrl + i, ws[i]);
	auto area = [&r](int i) { return (long long)r[i].width () * r[i].height (); };
	for (int i = 0; i < n; i++) {
		if (!ws[i]) continue;
		for (int k = 0; k < n; k++) {
			if (k == i || !ws[k] || !cont[k] || !r[k].contains (r[i].center ())) continue;
			if (!(area (k) > area (i) || (area (k) == area (i) && k < i))) continue;
			if (parent[i] < 0 || area (k) < area (parent[i])) parent[i] = k;
		}
	}

	QStringList pairs;
	std::function<void (UiWidget &, int)> build = [&](UiWidget &into, int p) {
		std::vector<int> kids;
		for (int pass = 0; pass < 2; pass++) // group boxes first; each part in reverse creation order, as Orbiter stacks them
			for (int i = n - 1; i >= 0; i--)
				if (ws[i] && parent[i] == p && ((d->ctrl[i].kind == RES_GROUPBOX) == (pass == 0))) kids.push_back (i);
		for (int i : kids) {
			const RESCONTROL *c = d->ctrl + i;
			QWidget *w = ws[i];
			UiWidget u;
			u.name = names[i];
			const bool standIn = IsStandIn (c, w);
			u.cls = (standIn ? "QFrame" : DesignerClass (w));
			u.SetProp ("geometry", UiValue::Rect (r[i].translated (p >= 0 ? -r[p].topLeft () : QPoint (0, 0))));
			if (standIn) {
				u.SetProp ("toolTip", UiValue::String (StandInTip (c)));
				u.SetProp ("frameShape", UiValue::Enum ("QFrame::Box"));
				u.SetProp ("frameShadow", UiValue::Enum ("QFrame::Plain"));
			} else if (QLabel *l = qobject_cast<QLabel*> (w)) {
				if (c->kind == RES_STATICIMAGE) {
					const RESIMAGE *ri = oapiFindResImage (hModule, c->imgid);
					const QString raw = (ri && ri->name ? QString::fromUtf8 (ri->name) : QString ());
					QString img = raw;
					for (QChar &ch : img)
						if (ch.unicode () >= 128 || (!ch.isLetterOrNumber () && ch != '_' && ch != '-')) ch = '_';
					if (img.isEmpty () || img != raw) img += QString ("_%1").arg (c->imgid);
					QString path = "images/" + img + ".png";
					if (!l->pixmap ().isNull ()) {
						u.SetProp ("pixmap", UiValue::Pixmap (path));
						if (std::none_of (images.begin (), images.end (), [&path](const LayoutImage &x) { return x.path == path; }))
							images.push_back ({path, l->pixmap ().toImage ()});
					}
				} else {
					u.SetProp ("text", UiValue::String (l->text ()));
					u.SetProp ("wordWrap", UiValue::Bool (l->wordWrap ()));
				}
				u.SetProp ("alignment", UiValue::Set (AlignText (l->alignment ())));
				if (l->frameShape () != QFrame::NoFrame) {
					u.SetProp ("frameShape", UiValue::Enum (l->frameShape () == QFrame::Panel ? "QFrame::Panel" : "QFrame::Box"));
					u.SetProp ("frameShadow", UiValue::Enum (l->frameShadow () == QFrame::Sunken ? "QFrame::Sunken" : "QFrame::Plain"));
				}
			} else if (QAbstractButton *b = qobject_cast<QAbstractButton*> (w)) {
				u.SetProp ("text", UiValue::String (b->text ()));
				if (QPushButton *pb = qobject_cast<QPushButton*> (w)) {
					if (pb->isFlat ()) u.SetProp ("flat", UiValue::Bool (true));
					if (pb->isCheckable ()) u.SetProp ("checkable", UiValue::Bool (true));
				}
			} else if (QGroupBox *g = qobject_cast<QGroupBox*> (w)) {
				u.SetProp ("title", UiValue::String (g->title ()));
				if (g->isFlat ()) u.SetProp ("flat", UiValue::Bool (true));
			} else if (QLineEdit *e = qobject_cast<QLineEdit*> (w)) {
				if (!e->hasFrame ()) u.SetProp ("frame", UiValue::Bool (false));
				if (e->isReadOnly ()) u.SetProp ("readOnly", UiValue::Bool (true));
				u.SetProp ("alignment", UiValue::Set (AlignText (e->alignment ())));
			} else if (QPlainTextEdit *e = qobject_cast<QPlainTextEdit*> (w)) {
				if (e->frameShape () == QFrame::NoFrame) u.SetProp ("frameShape", UiValue::Enum ("QFrame::NoFrame"));
				if (e->isReadOnly ()) u.SetProp ("readOnly", UiValue::Bool (true));
			} else if (QComboBox *cb = qobject_cast<QComboBox*> (w)) {
				if (cb->isEditable ()) u.SetProp ("editable", UiValue::Bool (true));
			} else if (QAbstractItemView *iv = qobject_cast<QAbstractItemView*> (w)) {
				if (iv->frameShape () == QFrame::NoFrame) u.SetProp ("frameShape", UiValue::Enum ("QFrame::NoFrame"));
			} else if (QScrollBar *sb = qobject_cast<QScrollBar*> (w)) {
				u.SetProp ("orientation", UiValue::Enum (sb->orientation () == Qt::Vertical ? "Qt::Vertical" : "Qt::Horizontal"));
			} else if (QSlider *sl = qobject_cast<QSlider*> (w)) {
				u.SetProp ("orientation", UiValue::Enum (sl->orientation () == Qt::Vertical ? "Qt::Vertical" : "Qt::Horizontal"));
			} else if (QProgressBar *pr = qobject_cast<QProgressBar*> (w)) {
				u.SetProp ("orientation", UiValue::Enum (pr->orientation () == Qt::Vertical ? "Qt::Vertical" : "Qt::Horizontal"));
			}
			if (!standIn && !w->styleSheet ().isEmpty ()) u.SetProp ("styleSheet", UiValue::String (w->styleSheet ()));
			const QString pair = QString ("%1:%2").arg (i).arg (c->id);
			u.SetDyn ("orbiterCtl", UiValue::String (pair));
			u.SetDyn ("orbiterFp", UiValue::String (Fingerprint (c)));
			if (standIn) u.SetDyn ("orbiterStandIn", UiValue::Bool (true));
			QString note = Note (dname, c);
			if (!note.isEmpty ()) u.SetDyn ("orbiterNote", UiValue::String (note));
			build (u, i);
			into.children.push_back (std::move (u));
		}
	};
	build (root, -1);
	QStringList fps;
	for (int i = 0; i < n; i++)
		if (ws[i]) pairs.append (QString ("%1:%2").arg (i).arg (d->ctrl[i].id)), fps.append (Fingerprint (d->ctrl + i));

	root.SetDyn ("orbiterBaseX", UiValue::Double (dlg->property ("resBaseX").toDouble ()));
	root.SetDyn ("orbiterBaseY", UiValue::Number (dlg->property ("resBaseY").toInt ()));
	root.SetDyn ("orbiterControls", UiValue::String (pairs.join (',')));
	root.SetDyn ("orbiterFps", UiValue::String (fps.join (','))); // parallel to orbiterControls

	for (QWidget *w : StockTabOrder (d, ws))
		for (int i = 0; i < n; i++)
			if (ws[i] == w) form.tabstops.append (names[i]);
	std::function<int (const UiWidget &)> count = [&count](const UiWidget &u) {
		int k = (int)u.children.size ();
		for (const auto &c : u.children) k += count (c);
		return k;
	};
	form.widgets = count (root);
	return form;
}

void UndoLayouts ()
{
	State &s = S ();
	if (s.undone) return;
	s.undone = true;
	for (const auto &w : s.decorations)
		if (w) w->deleteLater ();
	for (const auto &w : s.decorations)
		if (w) w->hide ();
	for (auto it = s.changes.rbegin (); it != s.changes.rend (); ++it)
		Revert (*it);
	for (const auto &f : s.filters)
		if (f) delete f.data ();
	for (const Hidden &h : s.hidden)
		if (h.w && !h.managed) h.w->show ();
	s.changes.clear ();
	s.decorations.clear ();
	s.hidden.clear ();
	s.filters.clear ();
	s.viewHidden.clear ();
}

void ResetLayoutState ()
{
	S () = State ();
}

int CopyStyleFiles (const QString &qss, const QString &srcDir, const QString &dstDir, QStringList &problems)
{
	const QString src = QDir (srcDir).canonicalPath (), dst = QDir::cleanPath (QDir (dstDir).absolutePath ());
	QRegularExpression ref ("\\$\\{SKIN\\}/([^)\"'\\s]+)");
	QStringList seen;
	qint64 total = 0;
	int n = 0;
	for (auto it = ref.globalMatch (qss); it.hasNext (); ) {
		const QString rel = it.next ().captured (1);
		if (seen.contains (rel)) continue;
		seen << rel;
		if (QDir::isAbsolutePath (rel) || rel.split ('/').contains ("..")) {
			problems << "the style sheet names " + rel + ", which is not a path inside its folder";
			continue;
		}
		const QString from = QFileInfo (src + "/" + rel).canonicalFilePath ();
		if (src.isEmpty () || from.isEmpty () || !from.startsWith (src + "/") || !QFileInfo (from).isFile ()) {
			problems << "the style sheet names " + rel + ", which is not a file in its skin folder";
			continue;
		}
		const QString to = QDir::cleanPath (dst + "/" + rel);
		if (!to.startsWith (dst + "/")) {
			problems << "the style sheet names " + rel + ", which would land outside the new skin folder";
			continue;
		}
		const qint64 size = QFileInfo (from).size ();
		if (n >= COPY_MAX_FILES || total + size > COPY_MAX_BYTES) {
			problems << "the style sheet names too many or too large files; the rest were not copied";
			break;
		}
		QDir ().mkpath (QFileInfo (to).absolutePath ());
		if (QFileInfo::exists (to) || QFile::copy (from, to)) {
			n++;
			total += size;
		} else problems << rel + " can't be copied";
	}
	return n;
}

void SetLayoutSkinView (QWidget *mainDlg, bool on)
{
	State &s = S ();
	Prune ();
	if (on) {
		for (const auto &w : s.decorations)
			if (w && w->parentWidget () == mainDlg && !w->isHidden ()) {
				w->hide ();
				s.viewHidden.push_back (w);
			}
	} else {
		for (const auto &w : s.viewHidden)
			if (w) w->show ();
		s.viewHidden.clear ();
	}
}

}
