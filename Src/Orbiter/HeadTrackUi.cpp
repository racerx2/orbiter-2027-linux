// not upstream (head-track): the Launchpad Controllers page's Head tracking group (HeadTrackUi.h)

#include "HeadTrackUi.h"
#include "HeadTrack.h"
#include "Orbiter.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>

extern Orbiter *g_pOrbiter;

static QString Q (const std::string &s) { return QString::fromStdString (s); }

static QString PoseText (const ht::TrackStatus &st)
{
	if (st.state != ht::HT_TRACKING) return QString ();
	if (!st.havePose) return QString ("%1 point%2 seen: the model needs 3").arg (st.blobs.size ()).arg (st.blobs.size () == 1 ? "" : "s");
	auto line = [](const char *what, const ht::RawPose &p) {
		return QString ("%1: yaw %2°  pitch %3°  roll %4°  x %5  y %6  z %7 mm").arg (what).arg (p.yaw, 0, 'f', 1).arg (p.pitch, 0, 'f', 1)
			.arg (p.roll, 0, 'f', 1).arg (p.tx, 0, 'f', 0).arg (p.ty, 0, 'f', 0).arg (p.tz, 0, 'f', 0);
	};
	return line ("Head", st.raw) + "\n" + line ("View", st.pose);
}

// ====================================================================================
// the points the TrackIR sees

class HeadTrackView : public QWidget {
public:
	explicit HeadTrackView (QWidget *parent) : QWidget (parent) { setMinimumSize (160, 120); setMaximumSize (240, 180); }
	void Set (const ht::TrackStatus &st) { m_st = st; update (); }
protected:
	void paintEvent (QPaintEvent *) override
	{
		QPainter p (this);
		p.setRenderHint (QPainter::Antialiasing);
		QRectF r = rect ().adjusted (0.5, 0.5, -0.5, -0.5);
		p.fillRect (r, QColor (20, 20, 24));
		p.setPen (QColor (70, 70, 80));
		p.drawLine (QPointF (r.center ().x (), r.top ()), QPointF (r.center ().x (), r.bottom ()));
		p.drawLine (QPointF (r.left (), r.center ().y ()), QPointF (r.right (), r.center ().y ()));
		p.drawRect (r);
		if (m_st.state != ht::HT_TRACKING || m_st.width <= 0 || m_st.height <= 0) return;
		double sx = r.width () / m_st.width, sy = r.height () / m_st.height;
		p.setPen (Qt::NoPen);
		p.setBrush (m_st.havePose ? QColor (120, 230, 120) : QColor (230, 180, 90));
		for (const ht::Blob &b : m_st.blobs) { // from the centre, +y up
			double rad = std::max (2.0, std::sqrt ((double)b.score) * sx);
			p.drawEllipse (QPointF (r.center ().x () + b.x * sx, r.center ().y () - b.y * sy), rad, rad);
		}
	}
private:
	ht::TrackStatus m_st;
};

// ====================================================================================
// the settings: Model, Axes, Views, Device; each change taken at once, kept by OK

class HeadTrackDialog : public QDialog {
public:
	HeadTrackDialog (HeadTrack *ht, QWidget *parent);
	ht::Settings work;
private:
	void Changed ();
	void Show ();
	HeadTrack *m_ht;
	std::vector<std::function<void ()>> m_load; // each control from the settings
	std::vector<std::pair<QFormLayout *, std::pair<QWidget *, const HtField *>>> m_rows;
	HeadTrackView *m_view;
	QLabel *m_pose, *m_fw;
	bool m_loading = false;
};

HeadTrackDialog::HeadTrackDialog (HeadTrack *ht, QWidget *parent) : QDialog (parent), work (ht->Settings ()), m_ht (ht)
{
	setWindowTitle ("Head tracking");
	auto *top = new QVBoxLayout (this);
	auto *tabs = new QTabWidget (this);
	top->addWidget (tabs);
	std::map<std::string, QFormLayout *> forms;
	for (const char *t : {"Model", "Axes", "Views", "Device"}) {
		auto *w = new QWidget (tabs);
		tabs->addTab (w, t);
		if (std::string (t) == "Axes") {
			auto *v = new QVBoxLayout (w);
			auto *g = new QGridLayout;
			v->addLayout (g);
			const auto &cols = HtAxisColumns ();
			for (int c = 0; c < (int)cols.size (); c++) {
				auto *h = new QLabel (cols[c].label, w);
				h->setToolTip (cols[c].tip);
				g->addWidget (h, 0, c + 1);
			}
			for (int a = 0; a < ht::AX_COUNT; a++) {
				g->addWidget (new QLabel (ht::AxisName (a), w), a + 1, 0);
				for (int c = 0; c < (int)cols.size (); c++) {
					const HtAxisColumn *col = &cols[c];
					if (col->check) {
						auto *cb = new QCheckBox (w);
						cb->setToolTip (col->tip);
						g->addWidget (cb, a + 1, c + 1, Qt::AlignCenter);
						m_load.push_back ([this, cb, col, a]() { cb->setChecked (col->get (work.axes[a]) != 0); });
						connect (cb, &QCheckBox::toggled, this, [this, col, a](bool on) { col->set (work.axes[a], on ? 1 : 0); Changed (); });
					} else {
						auto *sp = new QDoubleSpinBox (w);
						sp->setRange (col->lo, col->hi), sp->setSingleStep (col->step), sp->setDecimals (col->decimals);
						sp->setToolTip (col->tip);
						g->addWidget (sp, a + 1, c + 1);
						m_load.push_back ([this, sp, col, a]() { sp->setValue (col->get (work.axes[a])); });
						connect (sp, &QDoubleSpinBox::valueChanged, this, [this, col, a](double x) { col->set (work.axes[a], x); Changed (); });
					}
				}
			}
			auto *row = new QHBoxLayout;
			auto *def = new QPushButton ("Defaults", w);
			def->setToolTip ("linuxtrack's settings for every axis");
			row->addWidget (def), row->addStretch (1);
			v->addLayout (row);
			v->addStretch (1);
			connect (def, &QPushButton::clicked, this, [this]() {
				for (int a = 0; a < ht::AX_COUNT; a++) work.axes[a] = ht::DefaultAxis (a);
				Show ();
				Changed ();
			});
		} else forms[t] = new QFormLayout (w);
	}
	for (const HtField &f : HtFields ()) {
		QFormLayout *form = forms[f.tab];
		QWidget *w = form->parentWidget ();
		QWidget *ctl = nullptr;
		switch (f.kind) {
		case HtField::HEADING: {
			auto *l = new QLabel ("<b>" + QString (f.label).toHtmlEscaped () + "</b>", w);
			form->addRow (l);
			ctl = l;
			break;
		}
		case HtField::CHECK: {
			auto *cb = new QCheckBox (f.label, w);
			form->addRow (cb);
			m_load.push_back ([this, cb, &f]() { cb->setChecked (f.get (work) != 0); });
			connect (cb, &QCheckBox::toggled, this, [this, &f](bool on) { f.set (work, on ? 1 : 0); Changed (); });
			ctl = cb;
			break;
		}
		case HtField::NUMBER: {
			auto *sp = new QDoubleSpinBox (w);
			sp->setRange (f.lo, f.hi), sp->setSingleStep (f.step), sp->setDecimals (f.decimals);
			form->addRow (f.label, sp);
			m_load.push_back ([this, sp, &f]() { sp->setValue (f.get (work)); });
			connect (sp, &QDoubleSpinBox::valueChanged, this, [this, &f](double x) { f.set (work, x); Changed (); });
			ctl = sp;
			break;
		}
		case HtField::CHOICE: {
			auto *cb = new QComboBox (w);
			for (const char *c = f.choices; *c; c += strlen (c) + 1) cb->addItem (c);
			form->addRow (f.label, cb);
			m_load.push_back ([this, cb, &f]() { cb->setCurrentIndex ((int)f.get (work)); });
			connect (cb, &QComboBox::currentIndexChanged, this, [this, &f](int i) { if (i >= 0) { f.set (work, i); Changed (); } });
			ctl = cb;
			break;
		}
		}
		if (f.tip && *f.tip) ctl->setToolTip (f.tip);
		m_rows.push_back ({form, {ctl, &f}});
	}
	// the firmware, on the Device tab
	QFormLayout *dev = forms["Device"];
	m_fw = new QLabel (dev->parentWidget ());
	m_fw->setWordWrap (true);
	m_fw->setTextInteractionFlags (Qt::TextSelectableByMouse);
	auto *find = new QPushButton ("Find firmware...", dev->parentWidget ());
	find->setToolTip ("The TrackIR 4 and the TrackIR 5 revisions 1 and 2 take NaturalPoint's firmware at every start. Pick the folder of the TrackIR "
		"software from a Windows install (or a linuxtrack firmware folder): Orbiter finds it there and keeps a copy in Config/HeadTrack");
	dev->addRow (new QLabel ("<b>Firmware</b>", dev->parentWidget ()));
	dev->addRow (m_fw);
	dev->addRow (find);
	m_fw->setText (Q (m_ht->FirmwareText ()));
	connect (find, &QPushButton::clicked, this, [this]() {
		QString dir = QFileDialog::getExistingDirectory (this, "The TrackIR software's folder", QDir::homePath ());
		if (dir.isEmpty ()) return;
		std::string msg;
		QApplication::setOverrideCursor (Qt::WaitCursor);
		bool ok = m_ht->FindFirmware (dir.toStdString (), msg);
		QApplication::restoreOverrideCursor ();
		m_fw->setText (Q (m_ht->FirmwareText ()));
		if (ok) QMessageBox::information (this, "Find firmware", Q (msg));
		else QMessageBox::warning (this, "Find firmware", Q (msg));
	});

	auto *live = new QHBoxLayout;
	m_view = new HeadTrackView (this);
	m_pose = new QLabel (this);
	m_pose->setWordWrap (true);
	live->addWidget (m_view), live->addWidget (m_pose, 1);
	top->addLayout (live);
	auto *bb = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	top->addWidget (bb);
	connect (bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect (bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto *t = new QTimer (this);
	connect (t, &QTimer::timeout, this, [this]() {
		ht::TrackStatus st = m_ht->Status ();
		m_view->Set (st);
		QString pt = PoseText (st);
		m_pose->setText (pt.isEmpty () ? Q (m_ht->StatusText ()) : pt);
	});
	t->start (50);
	Show ();
}

void HeadTrackDialog::Show ()
{
	m_loading = true;
	for (auto &l : m_load) l ();
	m_loading = false;
	for (auto &r : m_rows) r.first->setRowVisible (r.second.first, !r.second.second->shown || r.second.second->shown (work));
}

void HeadTrackDialog::Changed ()
{
	if (m_loading) return;
	m_ht->Preview (work); // the view and the tracker follow at once
	for (auto &r : m_rows) r.first->setRowVisible (r.second.first, !r.second.second->shown || r.second.second->shown (work));
}

// ====================================================================================
// the group on the page

HeadTrackBox::HeadTrackBox (QWidget *parent) : QGroupBox ("Head tracking", parent)
{
	HeadTrack *ht = g_pOrbiter->GetHeadTrack ();
	auto *l = new QVBoxLayout (this);
	auto *row = new QHBoxLayout;
	m_on = new QCheckBox ("TrackIR head tracking in flight", this);
	m_on->setToolTip ("A TrackIR 4 or 5 moves the cockpit view with your head (Head tracking... picks the views and the feel)");
	auto *centre = new QPushButton ("Centre", this);
	centre->setToolTip ("The head as it is now is the centre");
	auto *more = new QPushButton ("Head tracking...", this);
	row->addWidget (m_on), row->addStretch (1), row->addWidget (centre), row->addWidget (more);
	l->addLayout (row);
	auto *srow = new QHBoxLayout;
	m_status = new QLabel (this);
	m_status->setWordWrap (true);
	m_status->setTextInteractionFlags (Qt::TextSelectableByMouse);
	m_access = new QPushButton ("Give access", this);
	m_access->setToolTip (Q ("Writes this udev rule (your desktop asks for your password): " + HeadTrack::AccessHint ()));
	m_access->hide ();
	srow->addWidget (m_status, 1), srow->addWidget (m_access, 0, Qt::AlignTop);
	l->addLayout (srow);
	m_keys = new QLabel (this);
	m_keys->setWordWrap (true);
	l->addWidget (m_keys);
	m_live = new QWidget (this);
	auto *live = new QHBoxLayout (m_live);
	live->setContentsMargins (0, 0, 0, 0);
	m_view = new HeadTrackView (m_live);
	m_pose = new QLabel (m_live);
	m_pose->setWordWrap (true);
	live->addWidget (m_view), live->addWidget (m_pose, 1);
	m_live->hide ();
	l->addWidget (m_live);
	if (ht) m_on->setChecked (ht->Settings ().enable);
	setEnabled (ht != nullptr);
	connect (m_on, &QCheckBox::toggled, this, [this](bool on) {
		HeadTrack *ht = g_pOrbiter->GetHeadTrack ();
		if (!ht) return;
		ht::Settings s = ht->Settings ();
		s.enable = on;
		std::string err;
		if (!ht->SetSettings (s, err)) QMessageBox::warning (this, "Head tracking", Q (err));
	});
	connect (centre, &QPushButton::clicked, this, []() { if (HeadTrack *ht = g_pOrbiter->GetHeadTrack ()) ht->Center (); });
	connect (more, &QPushButton::clicked, this, [this]() { Settings (); });
	connect (m_access, &QPushButton::clicked, this, []() { if (HeadTrack *ht = g_pOrbiter->GetHeadTrack ()) ht->GiveAccess (); });
	Keys ();
}

HeadTrackBox::~HeadTrackBox ()
{
	HeadTrack *ht = g_pOrbiter ? g_pOrbiter->GetHeadTrack () : nullptr; // gone before the Launchpad at exit
	if (ht && m_using) ht->Release ();
}

void HeadTrackBox::Keys ()
{
	m_keys->setText ("Keys: " + Q (HeadTrack::KeysText (g_pOrbiter->GetInput ())));
}

void HeadTrackBox::Settings ()
{
	HeadTrack *ht = g_pOrbiter->GetHeadTrack ();
	if (!ht) return;
	ht::Settings orig = ht->Settings ();
	HeadTrackDialog dlg (ht, this);
	if (dlg.exec () != QDialog::Accepted) { ht->Preview (orig); return; }
	ht::Settings s = dlg.work;
	s.enable = ht->Settings ().enable, s.keysAdded = ht->Settings ().keysAdded;
	std::string err;
	if (!ht->SetSettings (s, err)) QMessageBox::warning (this, "Head tracking", Q (err));
}

bool HeadTrackBox::Tick (bool shown)
{
	HeadTrack *ht = g_pOrbiter->GetHeadTrack ();
	if (!ht) return false;
	if (shown != m_using) { // the TrackIR runs while the page shows: the live view
		m_using = shown;
		if (shown) ht->Acquire (); else ht->Release ();
	}
	if (!shown) return false;
	bool keys = ht->Poll (g_pOrbiter->GetInput ());
	if (keys) Keys ();
	std::string text = ht->StatusText ();
	if (text != m_text) m_text = text, m_status->setText (Q (text));
	ht::TrackStatus st = ht->Status ();
	m_access->setVisible (st.state == ht::HT_NO_ACCESS);
	m_access->setEnabled (!ht->GivingAccess ());
	m_live->setVisible (st.state == ht::HT_TRACKING); // the points and the numbers only while it tracks
	m_view->Set (st);
	m_pose->setText (PoseText (st));
	return keys;
}
