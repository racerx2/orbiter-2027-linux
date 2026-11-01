// GPSNav: the OpenStreetMap map's water tiles (MapData.h)
#include "MapData.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include "Orbitersdk.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <list>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;
using namespace gps::map;
using Clock = std::chrono::steady_clock;

namespace {

const char *const TileJson = "https://tiles.openfreemap.org/planet"; // names the current planet build's tile URL
const size_t MaxTiles = 256;     // in memory
const int InFlight = 2;          // tiles fetched at once
const auto Retry = std::chrono::seconds (60);
const qint64 MaxBytes = 8 << 20; // a tile, every layer
const int StallMs = 20000, TotalMs = 60000;

std::shared_ptr<WaterTile> Make (uint64_t k, const std::string &layer)
{
	std::vector<Feature> w;
	uint32_t extent;
	if (!DecodeLayer ((const uint8_t *)layer.data (), layer.size (), w, extent)) return nullptr;
	auto t = std::make_shared<WaterTile> ();
	t->Build (KeyZ (k), KeyX (k), KeyY (k), w, extent);
	return t;
}

bool UrlOk (const std::string &u)
{
	return u.compare (0, 8, "https://") == 0 && u.find ("{z}") != std::string::npos && u.find ("{x}") != std::string::npos && u.find ("{y}") != std::string::npos;
}

bool ReadFile (const std::string &path, std::string &out, size_t most)
{
	std::ifstream f (path, std::ios::binary);
	if (!f) return false;
	f.seekg (0, std::ios::end);
	std::streamoff n = f.tellg ();
	if (n < 0 || (size_t)n > most) return false;
	out.resize ((size_t)n);
	f.seekg (0);
	return f.read (out.data (), n) || n == 0;
}

// written whole or not at all: a tmp file renamed over the old
bool WriteFile (const std::string &path, const std::string &data)
{
	std::error_code ec;
	fs::create_directories (fs::path (path).parent_path (), ec);
	std::string tmp = path + ".tmp";
	{
		std::ofstream f (tmp, std::ios::binary | std::ios::trunc);
		if (!f || !f.write (data.data (), (std::streamsize)data.size ()) || !f.flush ()) { f.close (), fs::remove (tmp, ec); return false; }
	}
	fs::rename (tmp, path, ec);
	if (ec) fs::remove (tmp, ec);
	return !ec;
}

// the HTTP status of a finished reply (0: failed or cut off)
int Status (QNetworkReply *r)
{
	QVariant sv = r->attribute (QNetworkRequest::HttpStatusCodeAttribute);
	if (!sv.isValid ()) return 0;
	int st = sv.toInt ();
	return (st == 200 || st == 204) && r->error () != QNetworkReply::NoError ? 0 : st;
}

} // namespace

struct MapData::Impl {
	std::string dir, ua;              // the cache folder; the user agent
	// shared, under mx
	mutable std::mutex mx;
	struct Slot { std::shared_ptr<const WaterTile> t; std::list<uint64_t>::iterator at; };
	std::unordered_map<uint64_t, Slot> tiles;
	std::list<uint64_t> lru;          // most recently used first
	std::vector<uint64_t> queue;      // wanted
	bool download = false;
	int nDisk = 0, nNet = 0, nFail = 0;
	int64_t netBytes = 0;
	std::atomic<unsigned> gen {0};
	// the worker's own
	QThread *thread = nullptr;
	QObject *ctx = nullptr;
	QNetworkAccessManager *nam = nullptr;
	bool stopping = false;
	std::unordered_set<QNetworkReply *> live;
	std::unordered_set<uint64_t> busy;
	std::unordered_map<uint64_t, Clock::time_point> failed;
	std::string url;                  // the tile URL, {z}/{x}/{y}
	bool urlBusy = false;
	Clock::time_point urlFailed {};

	std::string Path (uint64_t k) const
	{
		return dir + "/" + std::to_string (KeyZ (k)) + "/" + std::to_string (KeyX (k)) + "/" + std::to_string (KeyY (k)) + ".water";
	}
	bool Loaded (uint64_t k) const { std::lock_guard<std::mutex> l (mx); return tiles.count (k) > 0; }
	void Store (uint64_t k, std::shared_ptr<const WaterTile> t)
	{
		std::lock_guard<std::mutex> l (mx);
		auto it = tiles.find (k);
		if (it != tiles.end ()) lru.erase (it->second.at), tiles.erase (it);
		lru.push_front (k);
		tiles[k] = {std::move (t), lru.begin ()};
		while (tiles.size () > MaxTiles) tiles.erase (lru.back ()), lru.pop_back ();
		gen++;
	}
	// a cached tile; a file that doesn't decode is deleted, to be fetched again
	bool FromDisk (uint64_t k)
	{
		std::string p = Path (k), layer;
		std::error_code ec;
		if (!fs::is_regular_file (p, ec) || !ReadFile (p, layer, (size_t)MaxBytes)) return false;
		auto t = Make (k, layer);
		if (!t) { fs::remove (p, ec); return false; }
		Store (k, std::move (t));
		std::lock_guard<std::mutex> l (mx);
		nDisk++;
		return true;
	}
	void Pump ()
	{
		if (stopping || !nam) return;
		std::vector<uint64_t> q;
		bool dl;
		{
			std::lock_guard<std::mutex> l (mx);
			q = queue, dl = download;
		}
		auto now = Clock::now ();
		for (uint64_t k : q) {
			if (Loaded (k) || busy.count (k)) continue;
			auto f = failed.find (k);
			if (f != failed.end () && now - f->second < Retry) continue;
			if (FromDisk (k) || !dl) continue;
			if (url.empty ()) GetUrl (); // the tiles wait for it
			else if ((int)busy.size () < InFlight) GetTile (k);
		}
	}
	QNetworkReply *Get (const std::string &u)
	{
		QNetworkRequest rq (QUrl (QString::fromStdString (u)));
		rq.setRawHeader ("User-Agent", QByteArray::fromStdString (ua));
		rq.setAttribute (QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual); // no cookies sent or kept
		rq.setAttribute (QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
		rq.setTransferTimeout (StallMs);
		QNetworkReply *r = nam->get (rq);
		live.insert (r);
		QObject::connect (r, &QNetworkReply::downloadProgress, ctx, [r] (qint64 got, qint64 total) { if (got > MaxBytes || total > MaxBytes) r->abort (); });
		QTimer::singleShot (TotalMs, r, [r] { r->abort (); });
		return r;
	}
	// the TileJSON's tile URL, else the last one seen
	void GetUrl ()
	{
		if (urlBusy || (urlFailed != Clock::time_point {} && Clock::now () - urlFailed < Retry)) return;
		urlBusy = true;
		QNetworkReply *r = Get (TileJson);
		QObject::connect (r, &QNetworkReply::finished, ctx, [this, r] {
			live.erase (r), r->deleteLater ();
			urlBusy = false;
			if (stopping) return;
			std::string u, saved = dir + "/url.txt";
			if (Status (r) == 200) {
				QJsonArray a = QJsonDocument::fromJson (r->readAll ()).object ().value ("tiles").toArray ();
				if (!a.isEmpty ()) u = a.at (0).toString ().toStdString ();
				if (UrlOk (u)) WriteFile (saved, u);
			}
			if (!UrlOk (u) && ReadFile (saved, u, 4096)) u.erase (u.find_last_not_of (" \r\n\t") + 1);
			if (UrlOk (u)) url = u;
			else urlFailed = Clock::now ();
			Pump ();
		});
	}
	void GetTile (uint64_t k)
	{
		std::string u = url;
		auto Put = [&u] (const char *key, int v) { size_t at = u.find (key); if (at != std::string::npos) u.replace (at, 3, std::to_string (v)); };
		Put ("{z}", KeyZ (k)), Put ("{x}", KeyX (k)), Put ("{y}", KeyY (k));
		busy.insert (k);
		QNetworkReply *r = Get (u);
		QObject::connect (r, &QNetworkReply::finished, ctx, [this, r, k] {
			live.erase (r), r->deleteLater ();
			busy.erase (k);
			if (stopping) return;
			int st = Status (r);
			QByteArray body = st == 200 ? r->readAll () : QByteArray ();
			std::string layer;
			std::shared_ptr<WaterTile> t;
			if ((st == 200 || st == 204) && WaterLayer ((const uint8_t *)body.constData (), (size_t)body.size (), layer)) t = Make (k, layer); // 204: no layers, land
			if (t) {
				WriteFile (Path (k), layer);
				Store (k, std::move (t));
				std::lock_guard<std::mutex> l (mx);
				nNet++, netBytes += body.size ();
			} else {
				failed[k] = Clock::now ();
				std::lock_guard<std::mutex> l (mx);
				nFail++;
			}
			Pump ();
		});
	}
};

MapData::MapData () : d (std::make_unique<Impl> ()) {}
MapData::~MapData () { Stop (); }

void MapData::Want (const std::vector<uint64_t> &keys, bool download)
{
	{
		std::lock_guard<std::mutex> l (d->mx);
		d->queue = keys, d->download = download;
	}
	if (!d->thread) {
		if (d->dir.empty ()) {
			d->dir = oapiResolvePath ("GPSNav/Cache/Map");
			d->ua = "OrbiterLinux-GPSNav/" + std::to_string (oapiGetOrbiterVersion ()) + " (+https://github.com/racerx2/orbiter-2027-linux)";
		}
		d->thread = new QThread;
		d->thread->setObjectName ("GPSNavMap");
		d->ctx = new QObject;
		d->ctx->moveToThread (d->thread);
		d->thread->start ();
		QMetaObject::invokeMethod (d->ctx, [this] { d->stopping = false, d->nam = new QNetworkAccessManager; });
	}
	QMetaObject::invokeMethod (d->ctx, [this] { d->Pump (); });
}

std::shared_ptr<const WaterTile> MapData::Get (uint64_t key)
{
	std::lock_guard<std::mutex> l (d->mx);
	auto it = d->tiles.find (key);
	if (it == d->tiles.end ()) return nullptr;
	d->lru.splice (d->lru.begin (), d->lru, it->second.at);
	return it->second.t;
}

unsigned MapData::Generation () const { return d->gen.load (); }

void MapData::Stop ()
{
	if (!d->thread) return;
	QMetaObject::invokeMethod (d->ctx, [this] {
		d->stopping = true;
		std::vector<QNetworkReply *> r (d->live.begin (), d->live.end ());
		for (QNetworkReply *x : r) x->abort ();
		delete d->nam; // and its replies
		d->nam = nullptr;
		d->live.clear (), d->busy.clear (), d->urlBusy = false;
	}, Qt::BlockingQueuedConnection);
	d->thread->quit ();
	d->thread->wait ();
	delete d->ctx;
	delete d->thread;
	d->ctx = nullptr, d->thread = nullptr;
	std::lock_guard<std::mutex> l (d->mx);
	d->queue.clear ();
}

std::string MapData::Summary () const
{
	std::lock_guard<std::mutex> l (d->mx);
	char b[160];
	snprintf (b, sizeof b, "%zu tiles in memory, %d from the cache, %d fetched (%lld KB), %d failed", d->tiles.size (), d->nDisk, d->nNet, (long long)(d->netBytes >> 10), d->nFail);
	return b;
}
