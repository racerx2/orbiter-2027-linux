// not upstream (input-bindings): Launchpad Options page "Controllers" (TabControls.h)

#include "TabControls.h"
#include "Orbiter.h"
#include "Config.h"
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

extern Orbiter* g_pOrbiter;

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

	auto *gd = new QGroupBox ("Controllers", body);
	auto *ld = new QVBoxLayout (gd);
	m_list = new QLabel (gd);
	m_list->setWordWrap (true);
	m_list->setTextInteractionFlags (Qt::TextSelectableByMouse);
	ld->addWidget (m_list);
	m_background = new QCheckBox ("Controllers work while Orbiter is in the background", gd);
	m_background->setToolTip ("For cockpits with more windows (external MFDs, a map on another screen): the controllers keep flying when another window has the focus");
	ld->addWidget (m_background);
	top->addWidget (gd);

	auto *gb = new QGroupBox ("Bindings", body);
	auto *lb = new QVBoxLayout (gb);
	m_where = new QLabel (gb);
	m_where->setWordWrap (true);
	m_where->setTextInteractionFlags (Qt::TextSelectableByMouse);
	lb->addWidget (m_where);
	auto *reload = new QPushButton ("Read Config/Input again", gb);
	reload->setSizePolicy (QSizePolicy::Fixed, QSizePolicy::Fixed);
	lb->addWidget (reload);
	top->addWidget (gb);
	top->addStretch (1);

	QObject::connect (m_background, &QCheckBox::toggled, &m_life, [this](bool on) { Cfg()->CfgJoystickPrm.bBackground = on; });
	QObject::connect (reload, &QPushButton::clicked, &m_life, [this]() {
		g_pOrbiter->OnOptionChanged (OPTCAT_JOYSTICK, OPTITEM_JOYSTICK_DEVICE);
		Tick ();
	});
	m_timer = new QTimer (hPage);
	QObject::connect (m_timer, &QTimer::timeout, &m_life, [this]() { Tick (); });
	m_timer->start (500); // plugged in or out while the page shows
	return TRUE;
}

void OptionsPage_Controls::UpdateControls(QWidget *hPage)
{
	if (m_background) m_background->setChecked (Cfg()->CfgJoystickPrm.bBackground);
	Tick ();
}

void OptionsPage_Controls::Tick()
{
	InputSystem *inp = g_pOrbiter->GetInput();
	if (!inp || !m_list || !m_list->isVisible()) return;
	inp->Engine().SetMuted (true); // nothing acts on the Launchpad
	inp->Poll (nullptr);
	QStringList rows;
	for (auto &d : inp->Devices().Devices()) {
		QString name = QString::fromStdString (inp->NameOf (d->id)).toHtmlEscaped();
		QString prod = QString::fromStdString (d->name).toHtmlEscaped();
		if (d->state == InputDevice::OPEN)
			rows << QString ("<b>%1</b>: %2, %3 buttons, %4 axes%5").arg (name, prod).arg (d->buttons.size()).arg (d->axes.size())
				.arg (d->hatPresent[0] ? ", hat" : "");
		else
			rows << QString ("<b>%1</b>: no access. %2").arg (prod, QString::fromStdString (InputAccessHint (*d)).toHtmlEscaped());
	}
	m_list->setText (rows.isEmpty() ? QString ("No controllers found") : rows.join ("<br>"));
	m_where->setText (QString ("Bindings are in %1: Global.cfg for every vessel, Vessels/&lt;class&gt;.cfg for one vessel class, "
		"Devices.cfg names the controllers. Files changed while Orbiter runs are read again within a second.")
		.arg (QString::fromStdString (inp->Dir()).toHtmlEscaped()));
}
