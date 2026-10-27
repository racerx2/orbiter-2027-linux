
// not upstream: Launchpad Options page "Earth streaming" (TabStream.h)

#include "TabStream.h"
#include "Orbiter.h"
#include "Config.h"
#include "Log.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointF>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>
#include <algorithm>
#include <climits>

extern Orbiter* g_pOrbiter;

static OptionsPage_Stream *s_page = nullptr;


static QString Bytes (uint64_t b)
{
	if (b >= ((uint64_t)10 << 30)) return QString ("%1 GB").arg (b / 1073741824.0, 0, 'f', 1);
	return QString ("%1 MB").arg (b / 1048576.0, 0, 'f', 1);
}

// the cfg's CREDIT lines as rich text, http(s) addresses as links
static QString CreditHtml (const std::vector<std::string> &lines)
{
	static const QRegularExpression url ("(https?://[^\\s<>\"]*[^\\s<>\".,;:)])");
	QStringList c;
	for (const std::string &l : lines) c << QString::fromStdString (l).toHtmlEscaped().replace (url, "<a href=\"\\1\">\\1</a>");
	return c.join ("<br>");
}

OptionsPage_Stream::OptionsPage_Stream(OptionsPageContainer* container)
	: OptionsPage(container)
{
	s_page = this;
}

OptionsPage_Stream::~OptionsPage_Stream()
{
	if (s_page == this) s_page = nullptr;
	if (m_timer) m_timer->stop();
	Cancel();
	Join();
}

void OptionsPage_Stream::StopPrefetch()
{
	if (!s_page) return;
	s_page->Cancel();
	if (s_page->m_fetchThread.joinable()) s_page->m_fetchThread.join();
	if (s_page->m_sizeThread.joinable()) s_page->m_sizeThread.join(); // a Clear finishes before the session's stream starts
	PendingClear pc = StreamPendingClear (s_page->m_pendingClear, false, [] { return TileStream::Busy (s_page->BodyDir()); });
	if (pc == PendingClear::Refuse) s_page->NotCleared ();
	else if (pc == PendingClear::Start) {
		s_page->StartClear (s_page->m_pendingKeep); // a Clear confirmed behind a count runs now
		s_page->m_sizeThread.join();
	}
}

// a Clear that waited for a count is refused: another Orbiter has the folder
void OptionsPage_Stream::NotCleared()
{
	m_pendingClear = false;
	QString s = QString::fromStdString (StreamClearText (0, true, std::string()));
	m_status->setText (s);
	LOGOUT ("TileStream: %s", s.toUtf8().constData());
	m_clear->setEnabled (!m_fetch);
	m_refresh->setEnabled (true);
	m_scanWanted = true;
	Estimate ();
}

void OptionsPage_Stream::Cancel()
{
	if (m_fetch) m_fetch->cancel = true;
}

void OptionsPage_Stream::Join()
{
	if (m_fetchThread.joinable()) m_fetchThread.join();
	if (m_sizeThread.joinable()) m_sizeThread.join();
}

std::string OptionsPage_Stream::BodyDir() const
{
	char p[512];
	Config *cfg = const_cast<OptionsPage_Stream*>(this)->Cfg();
	return cfg->PTexPath (p, sizeof p, "Earth") ? oapiResolvePath (p) : std::string();
}

std::string OptionsPage_Stream::CfgFile() const
{
	Config *cfg = const_cast<OptionsPage_Stream*>(this)->Cfg();
	return oapiResolvePath (cfg->ConfigPath ("TileStream"));
}

BOOL OptionsPage_Stream::OnInitDialog(QWidget *hPage)
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

	auto *gs = new QGroupBox ("Streaming", hPage);
	auto *ls = new QGridLayout (gs);
	m_enable = new QCheckBox ("Stream Earth imagery and elevation", gs);
	m_enable->setToolTip ("While the simulation runs, missing Earth tiles are fetched from the free servers listed in Config/TileStream.cfg");
	m_offline = new QCheckBox ("Offline: only tiles already on disk", gs);
	m_offline->setToolTip ("The simulation makes no tile requests, and a region download is refused");
	m_cap = new QSpinBox (gs);
	m_cap->setRange (0, INT_MAX);
	m_cap->setSingleStep (256);
	m_cap->setSuffix (" MB");
	m_cap->setSpecialValueText ("No limit");
	m_cap->setToolTip ("The oldest streamed tiles above this size are removed at the start of a session; levels 8 and below are kept");
	ls->addWidget (m_enable, 0, 0, 1, 2);
	ls->addWidget (m_offline, 1, 0, 1, 2);
	ls->addWidget (new QLabel ("Cache limit:", gs), 2, 0);
	ls->addWidget (m_cap, 2, 1);
	ls->setColumnStretch (1, 1);
	top->addWidget (gs);

	auto *gc = new QGroupBox ("Streamed tiles", hPage);
	auto *lc = new QGridLayout (gc);
	m_size = new QLabel ("Counting...", gc);
	m_size->setWordWrap (true);
	m_size->setMinimumHeight (2 * m_size->fontMetrics().lineSpacing()); // the buttons below never move under the mouse
	m_refresh = new QPushButton ("Refresh", gc);
	m_clear = new QPushButton ("Clear streamed tiles...", gc);
	m_base = new QCheckBox ("including base levels (8 and below)", gc);
	lc->addWidget (m_size, 0, 0);
	lc->addWidget (m_refresh, 0, 1);
	lc->addWidget (m_clear, 1, 0, Qt::AlignLeft);
	lc->addWidget (m_base, 2, 0, 1, 2);
	lc->setColumnStretch (0, 1);
	top->addWidget (gc);

	auto *gr = new QGroupBox ("Download a region", hPage);
	auto *lr = new QGridLayout (gr);
	m_where = new QComboBox (gr);
	m_lat = new QDoubleSpinBox (gr);
	m_lat->setRange (-90.0, 90.0);
	m_lat->setDecimals (4);
	m_lat->setSuffix ("°");
	m_lng = new QDoubleSpinBox (gr);
	m_lng->setRange (-180.0, 180.0);
	m_lng->setDecimals (4);
	m_lng->setSuffix ("°");
	m_radius = new QDoubleSpinBox (gr);
	m_radius->setRange (0.5, 500.0);
	m_radius->setDecimals (1);
	m_radius->setSuffix (" km");
	m_radius->setValue (10.0);
	m_level = new QSpinBox (gr);
	m_level->setRange (5, 18);
	m_level->setValue (16);
	m_level->setToolTip ("Finest tile level: 14 is about 20 km a tile, 16 about 5 km, 18 about 1.2 km");
	m_estimate = new QLabel (gr);
	m_estimate->setWordWrap (true);
	m_estimate->setToolTip ("Open ocean and tiles already on disk take nothing");
	m_download = new QPushButton ("Download...", gr);
	m_cancel = new QPushButton ("Cancel", gr);
	m_cancel->setEnabled (false);
	m_bar = new QProgressBar (gr);
	m_bar->setRange (0, 1);
	m_bar->setValue (0);
	m_status = new QLabel (gr);
	m_status->setWordWrap (true);
	lr->addWidget (new QLabel ("Centre:", gr), 0, 0);
	lr->addWidget (m_where, 0, 1, 1, 3);
	lr->addWidget (new QLabel ("Latitude:", gr), 1, 0);
	lr->addWidget (m_lat, 1, 1);
	lr->addWidget (new QLabel ("Longitude:", gr), 1, 2);
	lr->addWidget (m_lng, 1, 3);
	lr->addWidget (new QLabel ("Radius:", gr), 2, 0);
	lr->addWidget (m_radius, 2, 1);
	lr->addWidget (new QLabel ("Max level:", gr), 2, 2);
	lr->addWidget (m_level, 2, 3);
	lr->addWidget (m_estimate, 3, 0, 1, 4);
	auto *buttons = new QHBoxLayout;
	buttons->addWidget (m_download);
	buttons->addWidget (m_cancel);
	buttons->addStretch (1);
	lr->addLayout (buttons, 4, 0, 1, 4);
	lr->addWidget (m_bar, 5, 0, 1, 4);
	lr->addWidget (m_status, 6, 0, 1, 4);
	lr->setColumnStretch (1, 1);
	lr->setColumnStretch (3, 1);
	top->addWidget (gr);

	auto *gk = new QGroupBox ("Sources and credits", hPage);
	auto *lk = new QVBoxLayout (gk);
	m_credits = new QLabel (gk);
	m_credits->setWordWrap (true);
	m_credits->setTextFormat (Qt::RichText);
	m_credits->setOpenExternalLinks (true);
	m_credits->setTextInteractionFlags (Qt::TextBrowserInteraction);
	lk->addWidget (m_credits);
	top->addWidget (gk);
	top->addStretch (1);

	QObject::connect (m_enable, &QCheckBox::toggled, &m_life, [this] (bool on) { Cfg()->bTileStream = on; m_offline->setEnabled (on); });
	QObject::connect (m_offline, &QCheckBox::toggled, &m_life, [this] (bool on) { Cfg()->bTileStreamOffline = on; });
	QObject::connect (m_cap, QOverload<int>::of (&QSpinBox::valueChanged), &m_life, [this] (int v) { Cfg()->TileStreamCacheMB = v; m_capUser = true; });
	QObject::connect (m_refresh, &QPushButton::clicked, &m_life, [this] {
		if (m_sizeThread.joinable() || m_clearing || m_pendingClear || m_fetch) return; // a download counts when it ends
		m_scanWanted = true; // Tick counts, at most once every 10 s
		m_size->setText ("Counting...");
	});
	QObject::connect (m_clear, &QPushButton::clicked, &m_life, [this] { OnClear (); });
	QObject::connect (m_download, &QPushButton::clicked, &m_life, [this] { OnDownload (); });
	QObject::connect (m_cancel, &QPushButton::clicked, &m_life, [this] { Cancel (); m_status->setText ("Cancelling..."); });
	QObject::connect (m_where, QOverload<int>::of (&QComboBox::currentIndexChanged), &m_life, [this] (int i) {
		QVariant v = m_where->itemData (i);
		if (!v.isValid()) return;
		QPointF p = v.toPointF();
		QSignalBlocker b1 (m_lat), b2 (m_lng);
		m_lat->setValue (p.y());
		m_lng->setValue (p.x());
		Estimate ();
	});
	auto custom = [this] {
		QSignalBlocker b (m_where);
		m_where->setCurrentIndex (0);
		Estimate ();
	};
	QObject::connect (m_lat, QOverload<double>::of (&QDoubleSpinBox::valueChanged), &m_life, custom);
	QObject::connect (m_lng, QOverload<double>::of (&QDoubleSpinBox::valueChanged), &m_life, custom);
	QObject::connect (m_radius, QOverload<double>::of (&QDoubleSpinBox::valueChanged), &m_life, [this] { Estimate (); });
	QObject::connect (m_level, QOverload<int>::of (&QSpinBox::valueChanged), &m_life, [this] { Estimate (); });

	LoadBases ();
	m_timer = new QTimer (hPage);
	QObject::connect (m_timer, &QTimer::timeout, &m_life, [this] { Tick (); });
	m_timer->start (200);
	UpdateControls (hPage);
	Estimate ();
	return TRUE;
}

// the Earth surface bases (Config/Earth/Base/*.cfg: Name, Location = lng lat) and the cfg's last levels
void OptionsPage_Stream::LoadBases()
{
	std::vector<TsBodyCfg> bodies;
	std::string err;
	if (TsReadCfg (CfgFile(), bodies, err))
		for (const TsBodyCfg &b : bodies)
			if (b.name == "Earth") {
				m_lmax = b.surf.lmax;
				m_elmax = b.elev.kind.empty() ? 0 : b.elev.lmax;
				m_credits->setText (CreditHtml (b.credit));
			}
	std::string dir = Cfg()->CfgDirPrm.ConfigDir;
	std::replace (dir.begin(), dir.end(), '\\', '/');
	if (!dir.empty() && dir.back() != '/') dir += '/';
	std::vector<StreamBase> bases = StreamBaseList (oapiResolvePath ((dir + "Earth/Base").c_str()));
	QSignalBlocker block (m_where);
	m_where->clear();
	m_where->addItem ("Custom position");
	int ksc = 0;
	for (const StreamBase &b : bases) {
		if (b.name == "Cape Canaveral") ksc = m_where->count();
		m_where->addItem (QString::fromStdString (b.name), QPointF (b.lng, b.lat));
	}
	if (!ksc && m_where->count() > 1) ksc = 1;
	if (ksc) {
		m_where->setCurrentIndex (ksc);
		QPointF p = m_where->itemData (ksc).toPointF();
		QSignalBlocker b1 (m_lat), b2 (m_lng);
		m_lat->setValue (p.y());
		m_lng->setValue (p.x());
	}
}

void OptionsPage_Stream::UpdateControls(QWidget *hPage)
{
	if (!m_enable) return;
	{
		QSignalBlocker b1 (m_enable), b2 (m_offline), b3 (m_cap);
		m_enable->setChecked (Cfg()->bTileStream);
		m_offline->setChecked (Cfg()->bTileStreamOffline);
		m_offline->setEnabled (Cfg()->bTileStream);
		m_cap->setValue (Cfg()->TileStreamCacheMB);
	}
	m_scanWanted = true; // a session may have streamed more; counted while the page shows
	Layout (hPage);
}

void OptionsPage_Stream::Layout(QWidget *hPage)
{
	hPage->resize (HParent()->size()); // fills the pane; its scroll area takes the rest
}

void OptionsPage_Stream::UpdateConfig(QWidget *hPage)
{
	if (!m_enable) return;
	Cfg()->bTileStream = m_enable->isChecked();
	Cfg()->bTileStreamOffline = m_offline->isChecked();
	if (m_capUser) Cfg()->TileStreamCacheMB = m_cap->value(); // a larger cfg value is never cut to the box
}

// off the UI thread: the size of the streamed tiles and of every loose Earth tile
void OptionsPage_Stream::StartScan()
{
	m_scanWanted = false;
	m_nextScan = std::chrono::steady_clock::now() + std::chrono::seconds (10);
	m_size->setText ("Counting...");
	auto s = std::make_shared<Size>();
	m_sz = s;
	std::string dir = BodyDir();
	m_sizeThread = std::thread ([s, dir] {
		try {
			s->streamed = TileStream::CacheBytes (dir, &s->total, &s->tiles);
		} catch (const std::exception &e) {
			s->error = e.what();
		} catch (...) {
			s->error = "unknown error";
		}
		s->ready = true;
	});
}

// off the UI thread: deletes the streamed tiles, then counts again
void OptionsPage_Stream::StartClear(bool keep)
{
	m_pendingClear = false;
	m_clearing = true;
	m_scanWanted = false;
	m_nextScan = std::chrono::steady_clock::now() + std::chrono::seconds (10);
	auto s = std::make_shared<Size>();
	s->clear = true;
	m_sz = s;
	std::string dir = BodyDir();
	m_sizeThread = std::thread ([s, dir, keep] {
		try {
			s->cleared = (long)TileStream::Clear (dir, keep, &s->refused);
			s->streamed = TileStream::CacheBytes (dir, &s->total, &s->tiles);
		} catch (const std::exception &e) {
			s->error = e.what();
		} catch (...) {
			s->error = "unknown error";
		}
		s->ready = true;
	});
}

void OptionsPage_Stream::Estimate()
{
	int L = std::min (m_level->value(), m_lmax);
	std::vector<TileStream::TileKey> keys = TileStream::RegionKeys (m_lat->value(), m_lng->value(), m_radius->value(), L);
	uint64_t bytes = 0;
	for (const TileStream::TileKey &k : keys) bytes += 2 * 174904 + (k.L >= 4 && k.L <= m_elmax ? 134262 : 0);
	QString s;
	if (keys.size() >= 100000) s = "Too many tiles: make the radius or the level smaller.";
	else s = QString ("%1 tiles, levels 1 to %2, up to %3").arg (keys.size()).arg (L).arg (Bytes (bytes));
	if (m_level->value() > m_lmax) s += QString (" The imagery ends at level %1.").arg (m_lmax);
	m_estimate->setText (s);
	m_download->setEnabled (!m_fetch && !m_clearing && !m_pendingClear && !keys.empty() && keys.size() < 100000);
}

void OptionsPage_Stream::Tick()
{
	QWidget *hPage = HPage();
	bool shows = hPage && hPage->isVisible();
	if (shows && hPage->size() != HParent()->size()) Layout (hPage);
	if (m_sz && m_sz->ready && !m_sz->shown) { // also a result whose thread StopPrefetch joined
		if (m_sizeThread.joinable()) m_sizeThread.join();
		m_sz->shown = true;
		if (!m_sz->error.empty()) m_size->setText (QString ("Not counted: %1").arg (QString::fromStdString (m_sz->error)));
		else m_size->setText (QString ("Streamed: %1 in %2 tiles\nAll loose Earth tiles: %3").arg (Bytes (m_sz->streamed)).arg (m_sz->tiles).arg (Bytes (m_sz->total)));
		if (m_sz->clear) {
			m_clearing = false;
			QString s = QString::fromStdString (StreamClearText (m_sz->cleared, m_sz->refused, m_sz->error));
			m_status->setText (s);
			LOGOUT ("TileStream: %s", s.toUtf8().constData());
		} else if (!m_sz->error.empty()) LOGOUT ("TileStream: cache count failed: %s", m_sz->error.c_str());
		if (!m_pendingClear) {
			m_clear->setEnabled (!m_fetch);
			m_refresh->setEnabled (true);
			Estimate ();
		}
	}
	PendingClear pc = StreamPendingClear (m_pendingClear, m_sizeThread.joinable(), [this] { return TileStream::Busy (BodyDir()); });
	if (pc == PendingClear::Refuse) NotCleared ();
	else if (pc == PendingClear::Start) StartClear (m_pendingKeep); // the count it waited for has ended
	if (m_scanWanted && shows && !m_fetch && !m_clearing && !m_sizeThread.joinable() && std::chrono::steady_clock::now() >= m_nextScan) StartScan ();
	if (!m_fetch) return;
	const TileStream::Progress &p = m_fetch->p;
	m_bar->setRange (0, std::max (1, p.total.load()));
	m_bar->setValue (p.done);
	QString s = QString ("%1 of %2 tiles: %3 on disk, %4 open ocean or no imagery, %5 failed; %6 so far").arg (p.done.load()).arg (p.total.load()).arg (p.have.load()).arg (p.none.load()).arg (p.failed.load()).arg (Bytes (p.bytes));
	if (!m_fetch->finished) {
		if (!m_fetch->cancel) m_status->setText (s);
		return;
	}
	if (m_fetchThread.joinable()) m_fetchThread.join();
	s = QString::fromStdString (StreamFetchText (m_fetch->result, m_fetch->error, s.toStdString()));
	m_status->setText (s);
	LOGOUT ("TileStream: region download: %s", s.toUtf8().constData());
	m_fetch.reset();
	m_cancel->setEnabled (false);
	m_clear->setEnabled (!m_clearing && !m_pendingClear);
	Estimate ();
	m_scanWanted = true;
}

// a message box and true if the action may not start now
bool OptionsPage_Stream::Refused(StreamAct a)
{
	StreamState st;
	st.session = g_pOrbiter && g_pOrbiter->Stream();
	st.fetching = m_fetch || m_fetchThread.joinable();
	st.clearing = m_clearing;
	st.pendingClear = m_pendingClear;
	st.offline = Cfg()->bTileStreamOffline;
	st.streamOff = !Cfg()->bTileStream;
	std::string why = StreamRefusal (a, st);
	if (why.empty()) {
		st.busy = TileStream::Busy (BodyDir());
		why = StreamRefusal (a, st);
	}
	if (why.empty()) return false;
	QMessageBox::information (HPage(), a == StreamAct::Clear ? "Clear streamed tiles" : "Download a region", QString::fromStdString (why));
	return true;
}

void OptionsPage_Stream::OnClear()
{
	if (Refused (StreamAct::Clear)) return;
	bool keep = !m_base->isChecked();
	QString msg = "Delete the Earth tiles the stream has downloaded?\n\n";
	msg += keep ? "Levels 8 and below are kept, so the globe still shows when you are offline. "
		: "Base levels 8 and below are deleted too; they are fetched again when needed. ";
	msg += "Tiles you installed yourself, texture packs and the Archive folder are not touched.";
	if (QMessageBox::question (HPage(), "Clear streamed tiles", msg, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
	if (m_fetch || m_clearing || m_pendingClear) return; // started while the question was open
	m_size->setText ("Clearing...");
	m_status->clear();
	m_clear->setEnabled (false);
	m_refresh->setEnabled (false);
	m_download->setEnabled (false);
	if (m_sizeThread.joinable()) { // a count runs: Tick starts the Clear after it, the UI never waits
		m_pendingClear = true;
		m_pendingKeep = keep;
	} else StartClear (keep);
}

void OptionsPage_Stream::OnDownload()
{
	if (Refused (StreamAct::Download)) return;
	int L = std::min (m_level->value(), m_lmax);
	double radius = m_radius->value();
	std::vector<TileStream::TileKey> keys = TileStream::RegionKeys (m_lat->value(), m_lng->value(), radius, L);
	if (keys.empty() || keys.size() >= 100000) return;
	uint64_t bytes = 0;
	for (const TileStream::TileKey &k : keys) bytes += 2 * 174904 + (k.L >= 4 && k.L <= m_elmax ? 134262 : 0);
	QString where = m_where->currentIndex() > 0 ? m_where->currentText() : QString ("%1°, %2°").arg (m_lat->value(), 0, 'f', 4).arg (m_lng->value(), 0, 'f', 4);
	QString msg = QString ("Download %1 Earth tiles within %2 km of %3, down to level %4, and the building and road tiles under them?\n\nUp to %5 for the imagery (open ocean and tiles already on disk take nothing). "
		"The requests go to the free servers listed in Config/TileStream.cfg.").arg (keys.size()).arg (radius).arg (where).arg (L).arg (Bytes (bytes));
	uint64_t cap = (uint64_t)m_cap->value() << 20, now = m_sz && m_sz->ready ? m_sz->streamed : 0;
	if (cap && now + bytes > cap)
		msg += QString ("\n\nThis may go over the cache limit of %1: the next session removes the oldest streamed tiles to get under it. Raise the limit to keep them.").arg (Bytes (cap));
	if (QMessageBox::question (HPage(), "Download a region", msg, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes) return;
	if (m_fetch || m_clearing || m_pendingClear) return; // started while the question was open
	TileStream::Options o;
	o.cfgfile = CfgFile();
	std::string dir = BodyDir();
	o.bodyDir = [dir] (const std::string &) { return dir; };
	o.bodies = {"Earth"};
	o.capMB = 0; // the session's cap takes over later
	o.buildings = Cfg()->bTileStreamBuildings; // not upstream (design-preload): the region's building and road tiles too
	o.ways = Cfg()->bTileStreamWays;
	o.userAgent = "OrbiterLinux/" + std::to_string (oapiGetOrbiterVersion()) + " (+https://github.com/racerx2/orbiter-2027-linux)";
	o.log = [] (const std::string &s) { LOGOUT ("%s", s.c_str()); };
	auto f = std::make_shared<Fetch>();
	m_fetch = f;
	m_bar->setRange (0, (int)keys.size());
	m_bar->setValue (0);
	m_status->setText ("Starting...");
	m_download->setEnabled (false);
	m_cancel->setEnabled (true);
	m_clear->setEnabled (false);
	LOGOUT ("TileStream: region download of %d tiles around %s, %.1f km, level %d", (int)keys.size(), where.toUtf8().constData(), radius, L);
	m_fetchThread = std::thread ([o, keys, f] {
		try {
			f->result = TileStream::Prefetch (o, "Earth", keys, f->p, f->cancel);
		} catch (const std::exception &e) { // shown with its text
			f->error = e.what();
			f->result = 6;
		} catch (...) {
			f->error = "unknown error";
			f->result = 6;
		}
		f->finished = true;
	});
}
