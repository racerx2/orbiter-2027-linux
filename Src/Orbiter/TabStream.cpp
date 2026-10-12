
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
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <strings.h>

extern Orbiter* g_pOrbiter;
namespace fs = std::filesystem;

static OptionsPage_Stream *s_page = nullptr;

static QString Bytes (uint64_t b)
{
	if (b >= ((uint64_t)10 << 30)) return QString ("%1 GB").arg (b / 1073741824.0, 0, 'f', 1);
	return QString ("%1 MB").arg (b / 1048576.0, 0, 'f', 1);
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
	auto *top = new QVBoxLayout (hPage);
	top->setContentsMargins (6, 6, 6, 6);

	auto *gs = new QGroupBox ("Streaming", hPage);
	auto *ls = new QGridLayout (gs);
	m_enable = new QCheckBox ("Stream Earth imagery and elevation", gs);
	m_enable->setToolTip ("While the simulation runs, missing Earth tiles are fetched from the free servers listed in Config/TileStream.cfg");
	m_offline = new QCheckBox ("Offline: only tiles already on disk", gs);
	m_offline->setToolTip ("The simulation makes no tile requests");
	m_cap = new QSpinBox (gs);
	m_cap->setRange (0, 1048576);
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
	top->addStretch (1);

	QObject::connect (m_enable, &QCheckBox::toggled, hPage, [this] (bool on) { Cfg()->bTileStream = on; m_offline->setEnabled (on); });
	QObject::connect (m_offline, &QCheckBox::toggled, hPage, [this] (bool on) { Cfg()->bTileStreamOffline = on; });
	QObject::connect (m_cap, QOverload<int>::of (&QSpinBox::valueChanged), hPage, [this] (int v) { Cfg()->TileStreamCacheMB = v; });
	QObject::connect (m_refresh, &QPushButton::clicked, hPage, [this] { ScanSize (); });
	QObject::connect (m_clear, &QPushButton::clicked, hPage, [this] { OnClear (); });
	QObject::connect (m_download, &QPushButton::clicked, hPage, [this] { OnDownload (); });
	QObject::connect (m_cancel, &QPushButton::clicked, hPage, [this] { Cancel (); m_status->setText ("Cancelling..."); });
	QObject::connect (m_where, QOverload<int>::of (&QComboBox::currentIndexChanged), hPage, [this] (int i) {
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
	QObject::connect (m_lat, QOverload<double>::of (&QDoubleSpinBox::valueChanged), hPage, custom);
	QObject::connect (m_lng, QOverload<double>::of (&QDoubleSpinBox::valueChanged), hPage, custom);
	QObject::connect (m_radius, QOverload<double>::of (&QDoubleSpinBox::valueChanged), hPage, [this] { Estimate (); });
	QObject::connect (m_level, QOverload<int>::of (&QSpinBox::valueChanged), hPage, [this] { Estimate (); });

	LoadBases ();
	m_timer = new QTimer (hPage);
	QObject::connect (m_timer, &QTimer::timeout, hPage, [this] { Tick (); });
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
			}
	std::string dir = Cfg()->CfgDirPrm.ConfigDir;
	std::replace (dir.begin(), dir.end(), '\\', '/');
	if (!dir.empty() && dir.back() != '/') dir += '/';
	std::vector<std::pair<std::string, QPointF>> bases;
	std::error_code ec;
	for (fs::directory_iterator it (oapiResolvePath ((dir + "Earth/Base").c_str()), ec), end; !ec && it != end; it.increment (ec)) {
		if (it->path().extension() != ".cfg") continue;
		std::ifstream f (it->path());
		std::string line, name;
		double lng = 0.0, lat = 0.0;
		bool loc = false;
		while (std::getline (f, line) && line.compare (0, 6, "BEGIN_")) {
			size_t eq = line.find ('=');
			if (eq == std::string::npos) continue;
			std::string key = line.substr (0, eq), val = line.substr (eq + 1);
			key.erase (key.find_last_not_of (" \t") + 1);
			val.erase (0, val.find_first_not_of (" \t"));
			val.erase (val.find_last_not_of (" \t\r") + 1);
			if (!strcasecmp (key.c_str(), "Name")) name = val;
			else if (!strcasecmp (key.c_str(), "Location")) loc = (bool)(std::istringstream (val) >> lng >> lat);
		}
		if (!name.empty() && loc) bases.push_back ({name, QPointF (lng, lat)});
	}
	std::sort (bases.begin(), bases.end(), [] (const auto &a, const auto &b) { return a.first < b.first; });
	QSignalBlocker block (m_where);
	m_where->clear();
	m_where->addItem ("Custom position");
	int ksc = 0;
	for (const auto &b : bases) {
		if (b.first == "Cape Canaveral") ksc = m_where->count();
		m_where->addItem (QString::fromStdString (b.first), b.second);
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
	if (!m_fetch && !m_sizeThread.joinable()) ScanSize (); // a session may have streamed more
	int w = std::max (HParent()->width(), hPage->minimumSizeHint().width());
	QLayout *l = hPage->layout();
	hPage->resize (w, l && l->hasHeightForWidth() ? l->totalHeightForWidth (w) : hPage->sizeHint().height());
}

void OptionsPage_Stream::UpdateConfig(QWidget *hPage)
{
	if (!m_enable) return;
	Cfg()->bTileStream = m_enable->isChecked();
	Cfg()->bTileStreamOffline = m_offline->isChecked();
	Cfg()->TileStreamCacheMB = m_cap->value();
}

// off the UI thread: the size of the streamed tiles and of every loose Earth tile
void OptionsPage_Stream::ScanSize()
{
	if (m_sizeThread.joinable()) return;
	m_size->setText ("Counting...");
	auto s = std::make_shared<Size>();
	m_sz = s;
	std::string dir = BodyDir();
	m_sizeThread = std::thread ([s, dir] {
		s->streamed = TileStream::CacheBytes (dir, &s->total, &s->tiles);
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
	m_download->setEnabled (!m_fetch && !keys.empty() && keys.size() < 100000);
}

void OptionsPage_Stream::Tick()
{
	QWidget *hPage = HPage();
	if (hPage && hPage->isVisible() && hPage->width() < HParent()->width()) UpdateControls (hPage);
	if (m_sz && m_sz->ready && m_sizeThread.joinable()) {
		m_sizeThread.join();
		m_size->setText (QString ("Streamed: %1 in %2 tiles\nAll loose Earth tiles: %3").arg (Bytes (m_sz->streamed)).arg (m_sz->tiles).arg (Bytes (m_sz->total)));
		if (m_sz->cleared >= 0) m_status->setText (QString ("Cleared %1 streamed tiles.").arg (m_sz->cleared));
		m_clear->setEnabled (!m_fetch);
		m_refresh->setEnabled (true);
	}
	if (!m_fetch) return;
	const TileStream::Progress &p = m_fetch->p;
	m_bar->setRange (0, std::max (1, p.total.load()));
	m_bar->setValue (p.done);
	QString s = QString ("%1 of %2 tiles: %3 on disk, %4 open ocean or no imagery; %5 so far").arg (p.done.load()).arg (p.total.load()).arg (p.have.load()).arg (p.none.load()).arg (Bytes (p.bytes));
	if (!m_fetch->finished) {
		if (!m_fetch->cancel) m_status->setText (s);
		return;
	}
	if (m_fetchThread.joinable()) m_fetchThread.join();
	switch (m_fetch->result) {
	case 0: s = "Done. " + s; break;
	case 1: s = "Cancelled. " + s; break;
	case 2: s = "Stopped: no answer from the tile servers for two minutes. " + s; break;
	default: s = "Config/TileStream.cfg has no Earth: nothing to download."; break;
	}
	m_status->setText (s);
	LOGOUT ("TileStream: region download: %s", s.toUtf8().constData());
	m_fetch.reset();
	m_cancel->setEnabled (false);
	Estimate ();
	ScanSize ();
}

static bool SessionRunning(QWidget *parent)
{
	if (!g_pOrbiter || !g_pOrbiter->Stream()) return false;
	QMessageBox::information (parent, "Earth streaming", "A simulation session is running. Try again once it has ended.");
	return true;
}

void OptionsPage_Stream::OnClear()
{
	if (m_fetch || SessionRunning (HPage())) return;
	bool keep = !m_base->isChecked();
	QString msg = "Delete the Earth tiles the stream has downloaded?\n\n";
	msg += keep ? "Levels 8 and below are kept, so the globe still shows when you are offline. "
		: "Base levels 8 and below are deleted too; they are fetched again when needed. ";
	msg += "Tiles you installed yourself, texture packs and the Archive folder are not touched.";
	if (QMessageBox::question (HPage(), "Clear streamed tiles", msg, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
	if (m_sizeThread.joinable()) m_sizeThread.join();
	m_size->setText ("Clearing...");
	m_clear->setEnabled (false);
	m_refresh->setEnabled (false);
	auto s = std::make_shared<Size>();
	m_sz = s;
	std::string dir = BodyDir();
	m_sizeThread = std::thread ([s, dir, keep] {
		s->cleared = (long)TileStream::Clear (dir, keep);
		s->streamed = TileStream::CacheBytes (dir, &s->total, &s->tiles);
		s->ready = true;
	});
}

void OptionsPage_Stream::OnDownload()
{
	if (m_fetch || m_fetchThread.joinable() || SessionRunning (HPage())) return;
	int L = std::min (m_level->value(), m_lmax);
	double radius = m_radius->value();
	std::vector<TileStream::TileKey> keys = TileStream::RegionKeys (m_lat->value(), m_lng->value(), radius, L);
	if (keys.empty() || keys.size() >= 100000) return;
	uint64_t bytes = 0;
	for (const TileStream::TileKey &k : keys) bytes += 2 * 174904 + (k.L >= 4 && k.L <= m_elmax ? 134262 : 0);
	QString where = m_where->currentIndex() > 0 ? m_where->currentText() : QString ("%1°, %2°").arg (m_lat->value(), 0, 'f', 4).arg (m_lng->value(), 0, 'f', 4);
	QString msg = QString ("Download %1 Earth tiles within %2 km of %3, down to level %4?\n\nUp to %5 (open ocean and tiles already on disk take nothing). "
		"The requests go to the free servers listed in Config/TileStream.cfg.").arg (keys.size()).arg (radius).arg (where).arg (L).arg (Bytes (bytes));
	uint64_t cap = (uint64_t)m_cap->value() << 20, now = m_sz && m_sz->ready ? m_sz->streamed : 0;
	if (cap && now + bytes > cap)
		msg += QString ("\n\nThis may go over the cache limit of %1: the next session removes the oldest streamed tiles to get under it. Raise the limit to keep them.").arg (Bytes (cap));
	if (QMessageBox::question (HPage(), "Download a region", msg, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes) return;
	TileStream::Options o;
	o.cfgfile = CfgFile();
	std::string dir = BodyDir();
	o.bodyDir = [dir] (const std::string &) { return dir; };
	o.bodies = {"Earth"};
	o.capMB = 0; // the session's cap takes over later
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
		f->result = TileStream::Prefetch (o, "Earth", keys, f->p, f->cancel);
		f->finished = true;
	});
}
