// not upstream (input-bindings, controls-editor): Launchpad Options page "Controllers" (TabControls.h)

#include "TabControls.h"
#include "Orbiter.h"
#include "Config.h"
#include "InputEditor.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <cstring>

extern Orbiter* g_pOrbiter;

static QString Q (const std::string &s) { return QString::fromStdString (s); }

static const InputDevice *DeviceNamed (InputSystem *inp, const std::string &name)
{
	return InputEditor::Device (inp, name);
}

static void Reread () // Config/Input changed: read again, the session's vessel included
{
	g_pOrbiter->OnOptionChanged (OPTCAT_JOYSTICK, OPTITEM_JOYSTICK_DEVICE);
}

// ====================================================================================
// one binding: the control, and what it does

class BindDialog : public QDialog {
public:
	BindDialog (InputSystem *inp, const Binding &init, bool haveInput, bool vesselProfile, QWidget *parent);
	Binding Result () const { return m_res; }

private:
	enum Page { P_FUNC, P_KEY, P_AXIS, P_MFD, P_LAYER, P_NONE };
	void Tick ();
	void InputChanged ();
	Binding Collect () const;
	void Preview ();
	void FillFunctions ();

	InputSystem *m_inp;
	Binding m_b, m_res;
	bool m_have;
	std::unique_ptr<InputCaptureFilter> m_cap, m_keycap;
	QLabel *m_in, *m_inLive, *m_line, *m_outLive;
	QPushButton *m_capBtn, *m_keyBtn, *m_ok;
	QComboBox *m_layer, *m_mode, *m_view, *m_type, *m_key, *m_chan, *m_amode, *m_mfd, *m_mfdbtn;
	QCheckBox *m_zone, *m_pass, *m_shift, *m_ctrl, *m_alt, *m_invert, *m_half, *m_pickup, *m_latch;
	QDoubleSpinBox *m_zlo, *m_zhi, *m_rate, *m_dz, *m_sat, *m_curve, *m_scale, *m_aval;
	QSpinBox *m_lnum;
	QLineEdit *m_filter;
	QListWidget *m_funcs;
	QStackedWidget *m_pages;
	QWidget *m_zoneRow, *m_modeRow, *m_axisOpts, *m_btnOpts;
};

static QDoubleSpinBox *Spin (double lo, double hi, double step, double v, QWidget *p)
{
	auto *s = new QDoubleSpinBox (p);
	s->setRange (lo, hi), s->setSingleStep (step), s->setDecimals (3), s->setValue (v);
	return s;
}

BindDialog::BindDialog (InputSystem *inp, const Binding &init, bool haveInput, bool vesselProfile, QWidget *parent)
	: QDialog (parent), m_inp (inp), m_b (init), m_have (haveInput)
{
	setWindowTitle ("Binding");
	auto *top = new QVBoxLayout (this);

	// the control
	auto *gi = new QGroupBox ("Control", this);
	auto *li = new QVBoxLayout (gi);
	auto *row = new QHBoxLayout;
	m_in = new QLabel (gi);
	QFont f = m_in->font();
	f.setBold (true);
	m_in->setFont (f);
	m_capBtn = new QPushButton ("Press a control...", gi);
	m_capBtn->setToolTip ("Then press a button, move an axis through half its travel, push the hat, turn a dial, press a key, "
		"or press a mouse button (left and right with Shift, Ctrl or Alt)");
	row->addWidget (m_in, 1);
	row->addWidget (m_capBtn);
	li->addLayout (row);
	m_inLive = new QLabel (gi);
	li->addWidget (m_inLive);
	auto *fi = new QFormLayout;
	m_layer = new QComboBox (gi);
	m_layer->addItem ("Base (no shift layer)");
	for (int i = 1; i <= 8; i++) m_layer->addItem (QString ("Shift layer %1").arg (i));
	fi->addRow ("Layer", m_layer);
	m_zoneRow = new QWidget (gi);
	auto *zr = new QHBoxLayout (m_zoneRow);
	zr->setContentsMargins (0, 0, 0, 0);
	m_zone = new QCheckBox ("Part of the axis works as a button, from", m_zoneRow);
	m_zlo = Spin (-1, 1, 0.05, 0.9, m_zoneRow);
	m_zhi = Spin (-1, 1, 0.05, 1.0, m_zoneRow);
	zr->addWidget (m_zone), zr->addWidget (m_zlo), zr->addWidget (new QLabel ("to", m_zoneRow)), zr->addWidget (m_zhi), zr->addStretch (1);
	fi->addRow ("", m_zoneRow);
	m_modeRow = new QWidget (gi);
	auto *mr = new QHBoxLayout (m_modeRow);
	mr->setContentsMargins (0, 0, 0, 0);
	m_mode = new QComboBox (m_modeRow);
	m_mode->addItems ({"Normal (held: held; others once)", "Held while pressed", "Once on press", "Once on release",
		"Once on press and once on release (toggle switch)", "Toggle: press on, press off", "Repeat while held"});
	m_rate = Spin (0.5, 100, 1, 10, m_modeRow);
	m_rate->setSuffix (" per s");
	mr->addWidget (m_mode, 1), mr->addWidget (m_rate);
	fi->addRow ("Acts", m_modeRow);
	m_view = new QComboBox (gi);
	m_view->addItems ({"Any view", "Cockpit view only", "Outside view only"});
	fi->addRow ("View", m_view);
	m_pass = new QCheckBox ("The key or button also keeps doing what it does without this binding", gi);
	fi->addRow ("", m_pass);
	li->addLayout (fi);
	top->addWidget (gi);

	// what it does
	auto *go = new QGroupBox ("Does", this);
	auto *lo = new QVBoxLayout (go);
	m_type = new QComboBox (go);
	m_type->addItems ({"An Orbiter function", "A key or key combination", "An analog control", "An MFD button", "A shift layer",
		vesselProfile ? "Nothing (takes this control's Global binding away)" : "Nothing"});
	lo->addWidget (m_type);
	m_pages = new QStackedWidget (go);
	lo->addWidget (m_pages);

	auto *pf = new QWidget (m_pages); // function
	auto *lf = new QVBoxLayout (pf);
	lf->setContentsMargins (0, 0, 0, 0);
	m_filter = new QLineEdit (pf);
	m_filter->setPlaceholderText ("Search");
	m_funcs = new QListWidget (pf);
	m_funcs->setMinimumHeight (160);
	lf->addWidget (m_filter), lf->addWidget (m_funcs);
	m_pages->addWidget (pf);

	auto *pk = new QWidget (m_pages); // key
	auto *lk = new QHBoxLayout (pk);
	lk->setContentsMargins (0, 0, 0, 0);
	m_key = new QComboBox (pk);
	for (int k = 1; k < 256; k++) {
		std::string n = InputKeyName (k);
		if (!n.empty()) m_key->addItem (Q (n), k);
	}
	m_shift = new QCheckBox ("Shift", pk), m_ctrl = new QCheckBox ("Ctrl", pk), m_alt = new QCheckBox ("Alt", pk);
	m_keyBtn = new QPushButton ("Press the key...", pk);
	lk->addWidget (m_key, 1), lk->addWidget (m_shift), lk->addWidget (m_ctrl), lk->addWidget (m_alt), lk->addWidget (m_keyBtn);
	m_pages->addWidget (pk);

	auto *pa = new QWidget (m_pages); // analog control
	auto *la = new QFormLayout (pa);
	la->setContentsMargins (0, 0, 0, 0);
	m_chan = new QComboBox (pa);
	for (int c = 0; c < ICH_COUNT; c++) m_chan->addItem (Q (InputChannel (c).name), c);
	m_chan->setToolTip ("Pitch, Yaw, Bank, TransX/Y/Z: attitude and translation; Main, Retro, Hover (and single engines): throttles; "
		"ElevTrim, RudderTrim, Flaps; BrakeL/R, Brakes; LookX/Y, PanX/Y, FOV, Distance: views");
	la->addRow ("Control", m_chan);
	m_axisOpts = new QWidget (pa);
	auto *ao = new QGridLayout (m_axisOpts);
	ao->setContentsMargins (0, 0, 0, 0);
	m_invert = new QCheckBox ("Invert", m_axisOpts);
	m_half = new QCheckBox ("Centre to end only", m_axisOpts);
	m_half->setToolTip ("For the 0 to 1 controls: the axis from its centre to its end gives 0 to 1 (otherwise the whole travel)");
	m_pickup = new QCheckBox ("Wait until moved (pickup)", m_axisOpts);
	m_pickup->setToolTip ("A lever acts only once it has moved after a start or a vessel switch");
	m_dz = Spin (0, 0.95, 0.01, 0, m_axisOpts), m_sat = Spin (0.05, 1, 0.01, 1, m_axisOpts);
	m_curve = Spin (0.1, 10, 0.1, 1, m_axisOpts), m_scale = Spin (-10, 10, 0.1, 1, m_axisOpts);
	ao->addWidget (m_invert, 0, 0), ao->addWidget (m_half, 0, 1), ao->addWidget (m_pickup, 0, 2);
	ao->addWidget (new QLabel ("Dead zone", m_axisOpts), 1, 0), ao->addWidget (m_dz, 1, 1);
	ao->addWidget (new QLabel ("Saturation", m_axisOpts), 2, 0), ao->addWidget (m_sat, 2, 1);
	ao->addWidget (new QLabel ("Curve", m_axisOpts), 3, 0), ao->addWidget (m_curve, 3, 1);
	ao->addWidget (new QLabel ("Scale", m_axisOpts), 4, 0), ao->addWidget (m_scale, 4, 1);
	la->addRow (m_axisOpts);
	m_btnOpts = new QWidget (pa);
	auto *bo = new QHBoxLayout (m_btnOpts);
	bo->setContentsMargins (0, 0, 0, 0);
	m_amode = new QComboBox (m_btnOpts);
	m_amode->addItems ({"Value while held", "Step by", "Set to"});
	m_aval = Spin (-1, 1, 0.05, 1, m_btnOpts);
	bo->addWidget (m_amode), bo->addWidget (m_aval), bo->addStretch (1);
	la->addRow (m_btnOpts);
	m_outLive = new QLabel (pa);
	la->addRow (m_outLive);
	m_pages->addWidget (pa);

	auto *pm = new QWidget (m_pages); // MFD button
	auto *lm = new QHBoxLayout (pm);
	lm->setContentsMargins (0, 0, 0, 0);
	m_mfd = new QComboBox (pm);
	m_mfd->addItems ({"Left MFD", "Right MFD"});
	for (int i = 3; i <= 12; i++) m_mfd->addItem (QString ("MFD %1").arg (i));
	m_mfdbtn = new QComboBox (pm);
	for (int i = 1; i <= 20; i++) m_mfdbtn->addItem (QString ("Button %1").arg (i), i-1);
	m_mfdbtn->addItem ("PWR", IMFD_PWR), m_mfdbtn->addItem ("SEL", IMFD_SEL), m_mfdbtn->addItem ("MNU", IMFD_MNU);
	lm->addWidget (m_mfd), lm->addWidget (m_mfdbtn), lm->addStretch (1);
	m_pages->addWidget (pm);

	auto *pl = new QWidget (m_pages); // shift layer
	auto *ll = new QHBoxLayout (pl);
	ll->setContentsMargins (0, 0, 0, 0);
	m_lnum = new QSpinBox (pl);
	m_lnum->setRange (1, 8);
	m_latch = new QCheckBox ("Latch: press on, press off (otherwise while held)", pl);
	ll->addWidget (new QLabel ("Layer", pl)), ll->addWidget (m_lnum), ll->addWidget (m_latch), ll->addStretch (1);
	m_pages->addWidget (pl);

	m_pages->addWidget (new QLabel (vesselProfile ? "This vessel class doesn't use the control's binding from Global." : "", m_pages));
	top->addWidget (go);

	m_line = new QLabel (this);
	m_line->setWordWrap (true);
	m_line->setTextInteractionFlags (Qt::TextSelectableByMouse);
	top->addWidget (m_line);
	auto *bb = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_ok = bb->button (QDialogButtonBox::Ok);
	top->addWidget (bb);

	// the binding's values into the controls
	m_layer->setCurrentIndex (m_b.layer);
	m_zone->setChecked (m_b.in.zone);
	if (m_b.in.zone) m_zlo->setValue (m_b.in.zlo), m_zhi->setValue (m_b.in.zhi);
	m_mode->setCurrentIndex (m_b.mode);
	m_rate->setValue (m_b.rate);
	m_view->setCurrentIndex (m_b.view);
	m_pass->setChecked (m_b.pass);
	FillFunctions ();
	int page = P_FUNC;
	switch (m_b.out) {
	case IOUT_FUNC:
		page = P_FUNC;
		for (int i = 0; i < m_funcs->count(); i++) if (m_funcs->item (i)->data (Qt::UserRole).toInt() == m_b.func) m_funcs->setCurrentRow (i);
		break;
	case IOUT_KEY: page = P_KEY; break;
	case IOUT_AXIS: page = P_AXIS; break;
	case IOUT_MFD: page = P_MFD; break;
	case IOUT_LAYER: page = P_LAYER; break;
	case IOUT_NONE: page = (haveInput && m_b.line ? P_NONE : (m_b.in.kind == INK_AXIS ? P_AXIS : P_FUNC)); break;
	}
	if (m_b.out == IOUT_KEY) {
		m_key->setCurrentIndex (std::max (0, m_key->findData (m_b.key)));
		m_shift->setChecked (m_b.kmods & KMOD_SHIFT), m_ctrl->setChecked (m_b.kmods & KMOD_CTRL), m_alt->setChecked (m_b.kmods & KMOD_ALT);
	}
	if (m_b.out == IOUT_AXIS) {
		m_chan->setCurrentIndex (std::max (0, m_chan->findData (m_b.chan)));
		m_amode->setCurrentIndex (m_b.amode);
		m_aval->setValue (m_b.aval);
	}
	m_invert->setChecked (m_b.invert), m_half->setChecked (m_b.half), m_pickup->setChecked (m_b.pickup);
	m_dz->setValue (m_b.dz), m_sat->setValue (m_b.sat), m_curve->setValue (m_b.curve), m_scale->setValue (m_b.scale);
	if (m_b.out == IOUT_MFD) {
		m_mfd->setCurrentIndex (m_b.mfd);
		m_mfdbtn->setCurrentIndex (std::max (0, m_mfdbtn->findData (m_b.mfdbtn)));
	}
	if (m_b.out == IOUT_LAYER) m_lnum->setValue (std::max (1, m_b.lnum)), m_latch->setChecked (m_b.latch);
	m_type->setCurrentIndex (page);
	m_pages->setCurrentIndex (page);

	// signals
	auto changed = [this]() { InputChanged (); };
	connect (m_type, &QComboBox::currentIndexChanged, this, [this](int i) { m_pages->setCurrentIndex (i); InputChanged (); });
	for (QComboBox *c : {m_layer, m_mode, m_view, m_key, m_chan, m_amode, m_mfd, m_mfdbtn})
		connect (c, &QComboBox::currentIndexChanged, this, changed);
	for (QCheckBox *c : {m_zone, m_pass, m_shift, m_ctrl, m_alt, m_invert, m_half, m_pickup, m_latch})
		connect (c, &QCheckBox::toggled, this, changed);
	for (QDoubleSpinBox *s : {m_zlo, m_zhi, m_rate, m_dz, m_sat, m_curve, m_scale, m_aval})
		connect (s, &QDoubleSpinBox::valueChanged, this, changed);
	connect (m_lnum, &QSpinBox::valueChanged, this, changed);
	connect (m_funcs, &QListWidget::currentRowChanged, this, changed);
	connect (m_filter, &QLineEdit::textChanged, this, [this]() { FillFunctions (); InputChanged (); });
	connect (m_capBtn, &QPushButton::clicked, this, [this]() {
		m_keycap.reset ();
		m_cap.reset (new InputCaptureFilter (m_inp, InputCaptureFilter::INPUT));
		InputChanged ();
	});
	connect (m_keyBtn, &QPushButton::clicked, this, [this]() {
		m_cap.reset ();
		m_keycap.reset (new InputCaptureFilter (m_inp, InputCaptureFilter::KEY));
		InputChanged ();
	});
	connect (bb, &QDialogButtonBox::accepted, this, [this]() {
		Binding b = Collect ();
		std::string err;
		if (!m_have || !InputEditor::Check (b, err)) return;
		m_res = b;
		accept ();
	});
	connect (bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto *t = new QTimer (this);
	connect (t, &QTimer::timeout, this, [this]() { Tick (); });
	t->start (30);
	connect (this, &QDialog::finished, this, [this]() { m_cap.reset (); m_keycap.reset (); });

	if (!m_have) m_cap.reset (new InputCaptureFilter (m_inp, InputCaptureFilter::INPUT)); // a new binding starts with its control
	InputChanged ();
	resize (640, sizeHint().height());
}

void BindDialog::FillFunctions ()
{
	int cur = (m_funcs->currentItem() ? m_funcs->currentItem()->data (Qt::UserRole).toInt() : (m_b.out == IOUT_FUNC ? m_b.func : -1));
	QString flt = m_filter->text().trimmed();
	QSignalBlocker block (m_funcs);
	m_funcs->clear ();
	for (int f = 0; f < LKEY_COUNT; f++) {
		char key[64];
		g_pOrbiter->keymap.KeyText (f, key);
		QString s = QString (Keymap::FuncName (f)) + (key[0] ? QString ("   (key %1)").arg (key) : QString ());
		if (!flt.isEmpty() && !s.contains (flt, Qt::CaseInsensitive)) continue;
		auto *it = new QListWidgetItem (s, m_funcs);
		it->setData (Qt::UserRole, f);
		if (f == cur) m_funcs->setCurrentItem (it);
	}
}

void BindDialog::Tick ()
{
	m_inp->Touch ();
	m_inp->Poll (nullptr); // the Launchpad: muted, the controls' values kept current
	InputRef r;
	if (m_cap && m_inp->Captured (r)) {
		m_cap.reset ();
		bool axis = (r.kind == INK_AXIS);
		m_b.in = r;
		m_have = true;
		if (axis && m_type->currentIndex() != P_AXIS && !m_zone->isChecked()) m_type->setCurrentIndex (P_AXIS); // a whole axis drives an analog control
		InputChanged ();
	}
	int dik;
	uint16_t mods;
	if (m_keycap && m_keycap->KeyGot (dik, mods)) {
		m_keycap.reset ();
		m_key->setCurrentIndex (std::max (0, m_key->findData (dik)));
		m_shift->setChecked (mods & KMOD_SHIFT), m_ctrl->setChecked (mods & KMOD_CTRL), m_alt->setChecked (mods & KMOD_ALT);
		InputChanged ();
	}
	// live values
	QString live;
	if (m_have && m_b.in.kind == INK_AXIS) {
		bool valid;
		double x = m_inp->Engine().AxisValue (m_b.in.dev, m_b.in.code, &valid);
		live = valid ? QString ("Now: %1").arg (x, 0, 'f', 3) : QString ("Not connected");
		Binding b = Collect ();
		if (valid && b.out == IOUT_AXIS) m_outLive->setText (QString ("Gives: %1").arg (m_inp->Engine().Process (b, x), 0, 'f', 3));
		else m_outLive->clear ();
	} else m_outLive->clear ();
	m_inLive->setText (live);
}

void BindDialog::InputChanged ()
{
	bool axis = m_have && m_b.in.kind == INK_AXIS;
	bool zone = axis && m_zone->isChecked();
	bool keymouse = m_have && m_b.in.src != INSRC_DEV;
	m_in->setText (m_cap ? QString ("Waiting for a control...") : m_have ? Q (InputEditor::InputText ([&]() {
		Binding x;
		x.in = m_b.in;
		return x; }())) : QString ("No control yet"));
	m_capBtn->setText (m_cap ? "Waiting..." : "Press a control...");
	m_keyBtn->setText (m_keycap ? "Waiting..." : "Press the key...");
	m_zoneRow->setVisible (axis);
	m_modeRow->setVisible (!axis || zone);
	m_rate->setVisible (m_mode->currentIndex() == IMODE_REPEAT);
	m_pass->setVisible (keymouse);
	bool wholeAxis = axis && !zone;
	int ch = m_chan->currentData().toInt();
	m_axisOpts->setVisible (wholeAxis);
	m_btnOpts->setVisible (!wholeAxis);
	if (!wholeAxis) { // a button: a value while held on the attitude and view controls, a step or a setting on the others
		bool abs = InputChannel (ch).absolute;
		if (!abs && m_amode->currentIndex() != 0) {
			QSignalBlocker block (m_amode);
			m_amode->setCurrentIndex (0);
		}
		m_amode->setEnabled (abs);
	}
	m_half->setEnabled (InputChannel (ch).unipolar);
	m_pickup->setEnabled (InputChannel (ch).absolute);
	Preview ();
}

Binding BindDialog::Collect () const
{
	Binding b;
	b.note = m_b.note, b.tail = m_b.tail, b.line = m_b.line;
	b.in = m_b.in;
	bool axis = (b.in.kind == INK_AXIS);
	b.in.zone = axis && m_zone->isChecked();
	if (b.in.zone) b.in.zlo = m_zlo->value(), b.in.zhi = m_zhi->value();
	else b.in.zlo = b.in.zhi = 0;
	b.layer = m_layer->currentIndex();
	b.mode = (axis && !b.in.zone) ? IMODE_AUTO : (InputMode)m_mode->currentIndex();
	b.rate = m_rate->value();
	b.view = m_view->currentIndex();
	b.pass = (b.in.src != INSRC_DEV) && m_pass->isChecked();
	switch (m_type->currentIndex()) {
	case P_FUNC:
		b.out = IOUT_FUNC;
		b.func = (m_funcs->currentItem() ? m_funcs->currentItem()->data (Qt::UserRole).toInt() : -1);
		break;
	case P_KEY:
		b.out = IOUT_KEY;
		b.key = m_key->currentData().toInt();
		b.kmods = (m_shift->isChecked() ? KMOD_SHIFT : 0) | (m_ctrl->isChecked() ? KMOD_CTRL : 0) | (m_alt->isChecked() ? KMOD_ALT : 0);
		break;
	case P_AXIS:
		b.out = IOUT_AXIS;
		b.chan = m_chan->currentData().toInt();
		if (axis && !b.in.zone) {
			b.invert = m_invert->isChecked();
			b.half = InputChannel (b.chan).unipolar && m_half->isChecked();
			b.pickup = InputChannel (b.chan).absolute && m_pickup->isChecked();
			b.dz = m_dz->value(), b.sat = m_sat->value(), b.curve = m_curve->value(), b.scale = m_scale->value();
		} else {
			b.amode = (InputAxMode)m_amode->currentIndex();
			b.aval = m_aval->value();
		}
		break;
	case P_MFD:
		b.out = IOUT_MFD;
		b.mfd = m_mfd->currentIndex();
		b.mfdbtn = m_mfdbtn->currentData().toInt();
		break;
	case P_LAYER:
		b.out = IOUT_LAYER;
		b.lnum = m_lnum->value();
		b.latch = m_latch->isChecked();
		break;
	default:
		b.out = IOUT_NONE;
		break;
	}
	return b;
}

void BindDialog::Preview ()
{
	if (!m_have) {
		m_line->setText ("Press a control first.");
		m_ok->setEnabled (false);
		return;
	}
	Binding b = Collect ();
	std::string err;
	if (b.out == IOUT_FUNC && b.func < 0) err = "pick a function";
	bool ok = err.empty() && InputEditor::Check (b, err);
	m_line->setText (ok ? QString ("In the file: <tt>BIND %1</tt>").arg (Q (InputFormatBinding (b)).toHtmlEscaped())
		: QString ("<span style='color:#d05050'>%1</span>").arg (Q (err).toHtmlEscaped()));
	m_ok->setEnabled (ok);
}

// ====================================================================================
// a controller's axes through their whole travel

class CalibDialog : public QDialog {
public:
	CalibDialog (InputSystem *inp, InputEditor *ed, const std::string &dev, QWidget *parent);
	~CalibDialog () { m_inp->HoldBack (false); }

private:
	void Tick ();
	InputSystem *m_inp;
	InputEditor *m_ed;
	std::string m_dev;
	InputCalibrator m_cal;
	bool m_done = false;
	QTableWidget *m_tab;
	QLabel *m_help;
	QPushButton *m_doneBtn, *m_save;
};

CalibDialog::CalibDialog (InputSystem *inp, InputEditor *ed, const std::string &dev, QWidget *parent)
	: QDialog (parent), m_inp (inp), m_ed (ed), m_dev (dev)
{
	setWindowTitle (Q ("Calibrate " + dev));
	m_inp->HoldBack (true);
	auto *top = new QVBoxLayout (this);
	m_help = new QLabel ("Move every axis of this controller through its whole travel a few times: stick and pedals to "
		"both ends, levers and toe brakes from end to end. Then let go of everything and press Done moving.", this);
	m_help->setWordWrap (true);
	top->addWidget (m_help);
	m_tab = new QTableWidget (0, 4, this);
	m_tab->setHorizontalHeaderLabels ({"Axis", "Seen", "Rests at", "Kind"});
	m_tab->horizontalHeader()->setSectionResizeMode (QHeaderView::ResizeToContents);
	m_tab->horizontalHeader()->setSectionResizeMode (3, QHeaderView::Stretch);
	m_tab->verticalHeader()->hide ();
	m_tab->setEditTriggers (QAbstractItemView::NoEditTriggers);
	top->addWidget (m_tab);
	auto *row = new QHBoxLayout;
	m_doneBtn = new QPushButton ("Done moving", this);
	m_save = new QPushButton ("Save", this);
	m_save->setEnabled (false);
	auto *reset = new QPushButton ("Reset to the raw range", this);
	reset->setToolTip ("Removes this controller's calibration: each axis uses the whole range the device reports");
	auto *cancel = new QPushButton ("Cancel", this);
	row->addWidget (m_doneBtn), row->addWidget (m_save), row->addStretch (1), row->addWidget (reset), row->addWidget (cancel);
	top->addLayout (row);

	if (const InputDevice *d = DeviceNamed (inp, dev)) m_cal.Start (*d);
	m_tab->setRowCount ((int)m_cal.axes.size());
	for (int i = 0; i < (int)m_cal.axes.size(); i++) {
		m_tab->setItem (i, 0, new QTableWidgetItem (Q (InputAbsName (m_cal.axes[i].code))));
		auto *kind = new QComboBox (m_tab);
		kind->addItems ({"Not moved", "Centring (stick, rudder)", "Lever (throttle, toe brake)"});
		kind->setEnabled (false);
		m_tab->setCellWidget (i, 3, kind);
		connect (kind, &QComboBox::currentIndexChanged, this, [this, i](int k) { if (k > 0) m_cal.axes[i].centring = (k == 1); });
	}
	connect (m_doneBtn, &QPushButton::clicked, this, [this]() {
		if (const InputDevice *d = DeviceNamed (m_inp, m_dev)) m_cal.Finish (*d);
		m_done = true;
		m_doneBtn->setEnabled (false);
		m_save->setEnabled (!m_cal.Result().empty());
		m_help->setText ("Check the kind of each axis, then Save. A centring axis keeps where it rests as its centre; "
			"a lever's centre is the middle of its travel.");
		for (int i = 0; i < (int)m_cal.axes.size(); i++) {
			auto *kind = static_cast<QComboBox*> (m_tab->cellWidget (i, 3));
			QSignalBlocker b (kind);
			bool moved = m_cal.Moved (m_cal.axes[i]);
			kind->setCurrentIndex (!moved ? 0 : m_cal.axes[i].centring ? 1 : 2);
			kind->setEnabled (moved);
		}
		Tick ();
	});
	connect (m_save, &QPushButton::clicked, this, [this]() {
		std::string err;
		if (!m_ed->WriteCalibration (m_dev, m_cal.Result(), err)) { QMessageBox::warning (this, "Calibrate", Q (err)); return; }
		accept ();
	});
	connect (reset, &QPushButton::clicked, this, [this]() {
		std::string err;
		if (!m_ed->WriteCalibration (m_dev, {}, err)) { QMessageBox::warning (this, "Calibrate", Q (err)); return; }
		accept ();
	});
	connect (cancel, &QPushButton::clicked, this, &QDialog::reject);
	auto *t = new QTimer (this);
	connect (t, &QTimer::timeout, this, [this]() { Tick (); });
	t->start (30);
	resize (680, 420);
}

void CalibDialog::Tick ()
{
	m_inp->Touch ();
	m_inp->Poll (nullptr);
	const InputDevice *d = DeviceNamed (m_inp, m_dev);
	if (!d) { m_help->setText ("The controller was unplugged."); return; }
	if (!m_done) m_cal.Update (*d);
	for (int i = 0; i < (int)m_cal.axes.size(); i++) {
		const auto &a = m_cal.axes[i];
		const InputAxis *ax = d->Axis (a.code);
		m_tab->setItem (i, 1, new QTableWidgetItem (QString ("%1 to %2 (of %3 to %4)").arg (a.lo).arg (a.hi).arg (a.min).arg (a.max)));
		m_tab->setItem (i, 2, new QTableWidgetItem (m_done ? QString::number (a.rest) : ax ? QString ("now %1").arg (ax->value) : QString ()));
	}
}

// ====================================================================================
// the page

OptionsPage_Controls::OptionsPage_Controls(OptionsPageContainer* container)
	: OptionsPage(container)
{
}

OptionsPage_Controls::~OptionsPage_Controls()
{
	if (m_timer) m_timer->stop();
}

const HELPCONTEXT* OptionsPage_Controls::HelpContext() const
{
	static HELPCONTEXT hcontext = g_pOrbiter->DefaultHelpPage("/tab_joystick.htm");
	return &hcontext;
}

BOOL OptionsPage_Controls::OnInitDialog(QWidget *hPage)
{
	m_page = hPage;
	auto *outer = new QVBoxLayout (hPage); // the Launchpad's Options tab has no scroll bar: the page scrolls itself
	outer->setContentsMargins (0, 0, 0, 0);
	auto *scroll = new QScrollArea (hPage);
	scroll->setFrameShape (QFrame::NoFrame);
	scroll->setWidgetResizable (true);
	auto *body = new QWidget (scroll);
	scroll->setWidget (body);
	outer->addWidget (scroll);
	auto *top = new QVBoxLayout (body);
	top->setContentsMargins (6, 6, 6, 6);

	// the controllers
	auto *gd = new QGroupBox ("Controllers", body);
	auto *ld = new QVBoxLayout (gd);
	m_devs = new QTableWidget (0, 3, gd);
	m_devs->setHorizontalHeaderLabels ({"Name", "Controller", "State"});
	m_devs->horizontalHeader()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
	m_devs->horizontalHeader()->setSectionResizeMode (1, QHeaderView::Stretch);
	m_devs->horizontalHeader()->setSectionResizeMode (2, QHeaderView::ResizeToContents);
	m_devs->verticalHeader()->hide ();
	m_devs->setSelectionBehavior (QAbstractItemView::SelectRows);
	m_devs->setSelectionMode (QAbstractItemView::SingleSelection);
	m_devs->setEditTriggers (QAbstractItemView::NoEditTriggers);
	m_devs->setMinimumHeight (110);
	ld->addWidget (m_devs);
	auto *drow = new QHBoxLayout;
	m_rename = new QPushButton ("Rename...", gd);
	m_rename->setToolTip ("The name the bindings use for this controller; they are renamed with it");
	m_calib = new QPushButton ("Calibrate...", gd);
	drow->addWidget (m_rename), drow->addWidget (m_calib), drow->addStretch (1);
	ld->addLayout (drow);
	m_live = new QWidget (gd);
	auto *lv = new QVBoxLayout (m_live);
	lv->setContentsMargins (0, 0, 0, 0);
	m_liveGrid = new QGridLayout;
	lv->addLayout (m_liveGrid);
	m_liveButtons = new QLabel (m_live);
	m_liveButtons->setWordWrap (true);
	lv->addWidget (m_liveButtons);
	ld->addWidget (m_live);
	m_background = new QCheckBox ("Controllers work while Orbiter is in the background", gd);
	m_background->setToolTip ("For cockpits with more windows (external MFDs, a map on another screen): the controllers keep flying when another window has the focus");
	ld->addWidget (m_background);
	top->addWidget (gd);

	// the bindings
	auto *gb = new QGroupBox ("Bindings", body);
	auto *lb = new QVBoxLayout (gb);
	auto *prow = new QHBoxLayout;
	m_prof = new QComboBox (gb);
	m_prof->setEditable (true);
	m_prof->setInsertPolicy (QComboBox::NoInsert);
	m_prof->completer()->setFilterMode (Qt::MatchContains);
	m_prof->completer()->setCompletionMode (QCompleter::PopupCompletion);
	m_prof->setToolTip ("All vessels, or one vessel class (its bindings come on top of the ones for all vessels). Type to search.");
	prow->addWidget (new QLabel ("For", gb)), prow->addWidget (m_prof, 1);
	lb->addLayout (prow);
	m_binds = new QTableWidget (0, 2, gb);
	m_binds->setHorizontalHeaderLabels ({"Control", "Does"});
	m_binds->horizontalHeader()->setSectionResizeMode (QHeaderView::Stretch);
	m_binds->verticalHeader()->hide ();
	m_binds->setSelectionBehavior (QAbstractItemView::SelectRows);
	m_binds->setSelectionMode (QAbstractItemView::SingleSelection);
	m_binds->setEditTriggers (QAbstractItemView::NoEditTriggers);
	m_binds->setMinimumHeight (220);
	lb->addWidget (m_binds);
	auto *brow = new QHBoxLayout;
	auto *add = new QPushButton ("Add...", gb);
	add->setToolTip ("Press a button, move an axis or push the hat, then pick what it does");
	m_change = new QPushButton ("Change...", gb);
	m_remove = new QPushButton ("Remove", gb);
	auto *reset = new QPushButton ("Reset...", gb);
	reset->setToolTip ("Back to Orbiter's defaults: these bindings, every vessel class's, or the keyboard keys");
	brow->addWidget (add), brow->addWidget (m_change), brow->addWidget (m_remove), brow->addStretch (1), brow->addWidget (reset);
	lb->addLayout (brow);
	m_status = new QLabel (gb);
	m_status->setWordWrap (true);
	m_status->setTextInteractionFlags (Qt::TextSelectableByMouse);
	lb->addWidget (m_status);
	top->addWidget (gb);
	top->addStretch (1);

	InputSystem *inp = g_pOrbiter->GetInput();
	if (inp) m_ed.reset (new InputEditor (inp, InputEditor::VesselDir (inp)));

	QObject::connect (m_background, &QCheckBox::toggled, &m_life, [this](bool on) { Cfg()->CfgJoystickPrm.bBackground = on; });
	QObject::connect (m_rename, &QPushButton::clicked, &m_life, [this]() { Rename (); });
	QObject::connect (m_calib, &QPushButton::clicked, &m_life, [this]() { Calibrate (); });
	QObject::connect (add, &QPushButton::clicked, &m_life, [this]() { Edit (true); });
	QObject::connect (m_change, &QPushButton::clicked, &m_life, [this]() { Edit (false); });
	QObject::connect (m_binds, &QTableWidget::cellDoubleClicked, &m_life, [this](int, int) { Edit (false); });
	QObject::connect (m_remove, &QPushButton::clicked, &m_life, [this]() { Remove (); });
	QObject::connect (reset, &QPushButton::clicked, &m_life, [this]() { ResetDefaults (); });
	QObject::connect (m_devs, &QTableWidget::itemSelectionChanged, &m_life, [this]() { FillLive (); });
	QObject::connect (m_binds, &QTableWidget::itemSelectionChanged, &m_life, [this]() {
		int r = m_binds->currentRow();
		bool own = m_ed && r >= 0 && r < (int)m_ed->Profile().binds.size();
		m_change->setEnabled (r >= 0), m_remove->setEnabled (own);
	});
	QObject::connect (m_prof, &QComboBox::currentIndexChanged, &m_life, [this](int) {
		if (!m_ed) return;
		std::string p = m_prof->currentData().toString().toStdString();
		if (!p.empty() && p != m_ed->Name()) { m_ed->Open (p); FillBindings (); }
	});
	m_timer = new QTimer (hPage);
	QObject::connect (m_timer, &QTimer::timeout, &m_life, [this]() { Tick (); });
	m_timer->start (50); // live values, and controllers plugged in or out while the page shows
	if (m_ed) m_ed->Open ("Global");
	FillProfiles ();
	FillBindings ();
	return TRUE;
}

void OptionsPage_Controls::UpdateControls(QWidget *hPage)
{
	if (m_background) m_background->setChecked (Cfg()->CfgJoystickPrm.bBackground);
	hPage->resize (HParent()->size());
	Tick ();
}

void OptionsPage_Controls::Tick()
{
	InputSystem *inp = g_pOrbiter->GetInput();
	if (m_page && HParent() && m_page->size() != HParent()->size()) m_page->resize (HParent()->size()); // fills the pane, as Planet streaming
	if (!inp || !m_devs || !m_devs->isVisible()) return;
	inp->Engine().SetMuted (true); // nothing acts on the Launchpad
	inp->Touch ();
	inp->Poll (nullptr);
	FillDevices ();
	FillLive ();
}

void OptionsPage_Controls::FillDevices()
{
	InputSystem *inp = g_pOrbiter->GetInput();
	std::string key;
	for (auto &d : inp->Devices().Devices()) key += std::to_string (d->id) + ":" + std::to_string ((int)d->state) + ":" + inp->NameOf (d->id) + ";";
	if (key == m_devKey) return;
	m_devKey = key;
	std::string sel = SelectedDevice();
	QSignalBlocker block (m_devs);
	m_devs->setRowCount (0);
	for (auto &d : inp->Devices().Devices()) {
		if (d->state == InputDevice::GONE) continue;
		int r = m_devs->rowCount();
		m_devs->insertRow (r);
		bool open = (d->state == InputDevice::OPEN);
		std::string name = open ? inp->NameOf (d->id) : std::string();
		auto *n = new QTableWidgetItem (Q (name));
		n->setData (Qt::UserRole, Q (name));
		m_devs->setItem (r, 0, n);
		m_devs->setItem (r, 1, new QTableWidgetItem (QString ("%1 (%2 buttons, %3 axes%4)").arg (Q (d->name)).arg (d->buttons.size())
			.arg (d->axes.size()).arg (d->hatPresent[0] ? ", hat" : "")));
		auto *st = new QTableWidgetItem (open ? "Connected" : "No access");
		if (!open) st->setToolTip (Q (InputAccessHint (*d)));
		m_devs->setItem (r, 2, st);
		if (open && !strcasecmp (name.c_str(), sel.c_str())) m_devs->selectRow (r);
	}
	if (m_devs->currentRow() < 0 && m_devs->rowCount() > 0) m_devs->selectRow (0);
	bool any = !SelectedDevice().empty();
	m_rename->setEnabled (any), m_calib->setEnabled (any);
	if (!m_devs->rowCount()) m_liveButtons->setText ("No controllers found. Plug one in: it shows here at once.");
	m_liveDev.clear ();
}

std::string OptionsPage_Controls::SelectedDevice() const
{
	int r = m_devs ? m_devs->currentRow() : -1;
	if (r < 0 || !m_devs->item (r, 0)) return "";
	return m_devs->item (r, 0)->data (Qt::UserRole).toString().toStdString();
}

void OptionsPage_Controls::FillLive()
{
	InputSystem *inp = g_pOrbiter->GetInput();
	std::string dev = SelectedDevice();
	const InputDevice *d = dev.empty() ? nullptr : DeviceNamed (inp, dev);
	m_rename->setEnabled (d), m_calib->setEnabled (d);
	if (dev != m_liveDev) { // the bars for this controller's axes
		m_liveDev = dev;
		while (QLayoutItem *it = m_liveGrid->takeAt (0)) { delete it->widget(); delete it; }
		if (d)
			for (size_t i = 0; i < d->axes.size(); i++) {
				auto *bar = new QProgressBar (m_live);
				bar->setRange (-1000, 1000);
				bar->setTextVisible (false);
				bar->setMaximumHeight (12);
				int c = (int)(i % 2) * 2;
				m_liveGrid->addWidget (new QLabel (Q (InputAbsName (d->axes[i].code)), m_live), (int)i / 2, c);
				m_liveGrid->addWidget (bar, (int)i / 2, c + 1);
			}
	}
	if (!d) { if (!dev.empty()) m_liveButtons->clear (); return; }
	for (size_t i = 0; i < d->axes.size(); i++) {
		QLayoutItem *it = m_liveGrid->itemAtPosition ((int)i / 2, (int)(i % 2) * 2 + 1);
		auto *bar = it ? qobject_cast<QProgressBar*> (it->widget()) : nullptr;
		if (bar) bar->setValue ((int)(1000 * inp->Engine().AxisValue (dev, d->axes[i].code)));
	}
	QStringList down;
	for (size_t i = 0; i < d->btn.size() && i < d->buttons.size(); i++) if (d->btn[i]) down << QString ("B%1").arg (i+1);
	static const char *hd[] = {"UP", "RIGHT", "DOWN", "LEFT"};
	for (int h = 0; h < 4; h++) {
		if (!d->hatPresent[h]) continue;
		int x = d->hat[h][0], y = d->hat[h][1];
		int dir = y < 0 ? 0 : x > 0 ? 1 : y > 0 ? 2 : x < 0 ? 3 : -1;
		if (dir >= 0) down << QString ("HAT%1.%2").arg (h).arg (hd[dir]);
	}
	m_liveButtons->setText ("Pressed now: " + (down.isEmpty() ? QString ("nothing") : down.join (" ")));
}

void OptionsPage_Controls::FillProfiles()
{
	if (!m_ed) return;
	QSignalBlocker block (m_prof);
	m_prof->clear ();
	m_prof->addItem ("All vessels (Global)", "Global");
	for (auto &c : m_ed->Classes()) m_prof->addItem (Q ("Vessel class " + c), Q ("Vessels/" + c));
	int i = m_prof->findData (Q (m_ed->Name()));
	m_prof->setCurrentIndex (std::max (0, i));
}

void OptionsPage_Controls::FillBindings()
{
	if (!m_ed) return;
	int keep = m_binds->currentRow();
	QSignalBlocker block (m_binds);
	m_binds->setRowCount (0);
	const InputProfile &p = m_ed->Profile();
	QStringList warn;
	for (int i = 0; i < (int)p.binds.size(); i++) {
		const Binding &b = p.binds[i];
		m_binds->insertRow (i);
		m_binds->setItem (i, 0, new QTableWidgetItem (Q (InputEditor::InputText (b))));
		m_binds->setItem (i, 1, new QTableWidgetItem (Q (InputEditor::OutputText (b))));
		std::string c = m_ed->Conflict (b, i, &g_pOrbiter->keymap), also = m_ed->AlsoDoes (b, i);
		if (!c.empty()) m_binds->item (i, 0)->setText (Q ("⚠ " + InputEditor::InputText (b)));
		std::string tip = c + (c.empty() || also.empty() ? "" : "\n") + also;
		if (!tip.empty()) for (int k = 0; k < 2; k++) m_binds->item (i, k)->setToolTip (Q (tip));
	}
	for (auto &b : m_ed->Inherited()) { // what applies from Global (and INHERIT) without being replaced here
		int r = m_binds->rowCount();
		m_binds->insertRow (r);
		auto *a = new QTableWidgetItem (Q (InputEditor::InputText (b)));
		auto *o = new QTableWidgetItem (Q (InputEditor::OutputText (b) + "   (from Global)"));
		for (auto *it : {a, o}) it->setForeground (m_binds->palette().color (QPalette::Disabled, QPalette::Text));
		a->setToolTip ("For all vessels. Change... makes this vessel class's own binding for the control.");
		m_binds->setItem (r, 0, a), m_binds->setItem (r, 1, o);
	}
	for (auto &e : p.errors) warn << Q (e);
	m_status->setText (warn.isEmpty() ? QString ("Saved to %1 at once; Orbiter uses it at once.").arg (Q (m_ed->Name() + ".cfg"))
		: "<span style='color:#d05050'>" + warn.join ("<br>").toHtmlEscaped() + "</span>");
	if (keep >= 0 && keep < m_binds->rowCount()) m_binds->selectRow (keep);
	int r = m_binds->currentRow();
	m_change->setEnabled (r >= 0), m_remove->setEnabled (r >= 0 && r < (int)p.binds.size());
}

void OptionsPage_Controls::Reload()
{
	Reread ();
	m_devKey.clear ();
	if (m_ed) m_ed->Open (m_ed->Name());
	FillBindings ();
	Tick ();
}

void OptionsPage_Controls::Rename()
{
	std::string dev = SelectedDevice();
	if (dev.empty() || !m_ed) return;
	bool ok;
	QString to = QInputDialog::getText (m_devs, "Rename controller", "New name (letters, digits, _ and -); the bindings change with it:",
		QLineEdit::Normal, Q (dev), &ok).trimmed();
	if (!ok || to.isEmpty() || to.toStdString() == dev) return;
	std::string err;
	if (!m_ed->Rename (dev, to.toStdString(), err)) QMessageBox::warning (m_devs, "Rename controller", Q (err));
	Reload ();
}

void OptionsPage_Controls::Calibrate()
{
	std::string dev = SelectedDevice();
	if (dev.empty() || !m_ed) return;
	CalibDialog dlg (g_pOrbiter->GetInput(), m_ed.get(), dev, m_devs);
	if (dlg.exec() == QDialog::Accepted) Reload ();
}

void OptionsPage_Controls::Edit(bool add)
{
	if (!m_ed) return;
	const InputProfile &p = m_ed->Profile();
	int r = add ? -1 : m_binds->currentRow();
	Binding init;
	int index = -1;
	if (r >= 0 && r < (int)p.binds.size()) init = p.binds[r], index = r;
	else if (r >= (int)p.binds.size()) { // a Global binding: this class gets its own
		std::vector<Binding> inh = m_ed->Inherited();
		size_t k = (size_t)(r - (int)p.binds.size());
		if (k < inh.size()) init = inh[k], init.note.clear(), init.tail.clear(), init.line = 0;
	} else if (!add) return;
	bool vessel = strcasecmp (m_ed->Name().c_str(), "Global") != 0;
	BindDialog dlg (g_pOrbiter->GetInput(), init, !add, vessel, m_binds);
	if (dlg.exec() != QDialog::Accepted) return;
	std::string err;
	if (!m_ed->Put (index, dlg.Result(), err)) QMessageBox::warning (m_binds, "Binding", Q (err));
	Reload ();
	if (index < 0) m_binds->selectRow ((int)m_ed->Profile().binds.size() - 1);
}

void OptionsPage_Controls::Remove()
{
	if (!m_ed) return;
	int r = m_binds->currentRow();
	if (r < 0 || r >= (int)m_ed->Profile().binds.size()) return;
	std::string err;
	if (!m_ed->Remove (r, err)) QMessageBox::warning (m_binds, "Binding", Q (err));
	Reload ();
}

void OptionsPage_Controls::ResetDefaults()
{
	if (!m_ed) return;
	bool global = !strcasecmp (m_ed->Name().c_str(), "Global");
	std::string cls = m_ed->Name().substr (m_ed->Name().find ('/') + 1);
	size_t nv = m_ed->VesselProfiles().size();
	QDialog dlg (m_binds);
	dlg.setWindowTitle ("Reset to defaults");
	auto *l = new QVBoxLayout (&dlg);
	l->addWidget (new QLabel ("What goes back to Orbiter's defaults:", &dlg));
	auto *cp = new QCheckBox (global ? QString ("All vessels (Global): the bindings Orbiter first made from Orbiter.cfg's joystick settings")
		: Q ("Vessel class " + cls + ": its own bindings removed, so those for all vessels apply"), &dlg);
	auto *cv = new QCheckBox (QString ("Every vessel class's own bindings (%1 classes)").arg (nv), &dlg);
	auto *ck = new QCheckBox ("Keyboard keys (keymap.cfg): Orbiter's default keys", &dlg);
	auto *info = new QLabel ("The old files are kept next to them as .bak. Controller names and calibration stay.", &dlg);
	info->setWordWrap (true);
	cp->setChecked (true);
	cv->setVisible (nv > 0);
	auto *bb = new QDialogButtonBox (QDialogButtonBox::Cancel, &dlg);
	QPushButton *go = bb->addButton ("Reset", QDialogButtonBox::AcceptRole);
	for (QWidget *w : std::initializer_list<QWidget*> {cp, cv, ck, info, bb}) l->addWidget (w);
	auto any = [&]() { go->setEnabled (cp->isChecked() || cv->isChecked() || ck->isChecked()); };
	for (QCheckBox *c : {cp, cv, ck}) QObject::connect (c, &QCheckBox::toggled, &dlg, any);
	QObject::connect (bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect (bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	if (dlg.exec() != QDialog::Accepted) return;
	QStringList done;
	std::string note, err;
	bool ok = true;
	if (cp->isChecked() && (ok = m_ed->Reset (note, err))) done << (global ? QString ("all vessels (Global)") : Q ("vessel class " + cls));
	if (ok && cv->isChecked() && (ok = m_ed->ResetVessels (err))) done << QString ("every vessel class (%1)").arg (nv);
	if (ok && ck->isChecked() && (ok = InputEditor::ResetKeymap (g_pOrbiter->keymap, oapiResolvePath ("keymap.cfg"), err))) done << "the keyboard keys";
	Reload ();
	FillProfiles (); // the classes with a profile come first
	QString msg = done.isEmpty() ? QString() : "Back to the defaults: " + done.join (", ") + ". The old files are kept as .bak.";
	if (!note.empty()) msg += (msg.isEmpty() ? "" : "\n") + Q (note);
	if (!ok) QMessageBox::warning (m_binds, "Reset to defaults", Q (err) + (msg.isEmpty() ? QString() : "\n\n" + msg));
	if (!msg.isEmpty()) m_status->setText (msg.toHtmlEscaped().replace ("\n", "<br>"));
}
