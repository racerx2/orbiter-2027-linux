
// not upstream: HTTP GETs for the tile stream on a Qt thread of its own (TileStreamNet.h)

#include "TileStreamNet.h"
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

// a 206's Content-Range "bytes a-b/total": the asked start, no more than asked, as many bytes as it says
bool RangeOk (const QByteArray &cr, int64_t off, int64_t len, int64_t got)
{
	unsigned long long a, b, total = 0; // not upstream: unsigned, the range checked before any sum (no overflow)
	char tot[32] = "";
	if (off < 0 || len <= 0 || got < 0 || sscanf (cr.constData(), "bytes %llu-%llu/%31s", &a, &b, tot) < 2) return false;
	if (tot[0] && tot[0] != '*') total = strtoull (tot, nullptr, 10);
	unsigned long long last = (unsigned long long)off + (unsigned long long)len - 1;
	if (a != (unsigned long long)off || b < a || b > last || (unsigned long long)got != b - a + 1) return false;
	return b == last || (total > 0 && b == total - 1); // all of it, or up to the end of the file
}

}

TsNet::TsNet (const std::string &userAgent, int64_t maxBytes, int64_t maxRange, int maxMs)
: nam(nullptr), ua(QByteArray::fromStdString (userAgent)), cap(maxBytes), rangeCap(maxRange), totalMs(maxMs)
{
	thread = new QThread;
	thread->setObjectName ("TileStreamNet");
	ctx = new QObject;
	ctx->moveToThread (thread);
	thread->start();
	QMetaObject::invokeMethod (ctx, [this] { nam = new QNetworkAccessManager; }, Qt::BlockingQueuedConnection);
}

TsNet::~TsNet ()
{
	QMetaObject::invokeMethod (ctx, [this] { delete nam; nam = nullptr; }, Qt::BlockingQueuedConnection);
	thread->quit();
	thread->wait();
	delete ctx;
	delete thread;
}

void TsNet::Get (const std::string &url, Reply done, int64_t off, int64_t len, int ownMs, int stallMs, int64_t ownCap)
{
	int64_t most = ownCap > 0 ? ownCap : off >= 0 ? rangeCap : cap; // not upstream: ownCap
	int maxMs = ownMs > 0 ? ownMs : totalMs;
	int stall = stallMs > 0 ? stallMs : StallMs;
	QMetaObject::invokeMethod (ctx, [this, url, done, off, len, most, maxMs, stall] {
		bool called = false;
		auto reply = [&] (int st, QByteArray body) { called = true; done (st, std::move (body), 0); };
		try {
			if (!nam) return;
			if (off >= 0 && (len <= 0 || len > most || off > INT64_MAX - len)) { reply (0, QByteArray()); return; } // not upstream: a range that fits the cap and the number type
			QUrl qu (QString::fromStdString (url));
			if (off >= 0 && qu.isLocalFile()) { // a range of a local file, read here
				QFile f (qu.toLocalFile());
				if (!f.open (QIODevice::ReadOnly)) { reply (404, QByteArray()); return; }
				if (off >= f.size() || !f.seek (off)) { reply (416, QByteArray()); return; }
				reply (200, f.read (len));
				return;
			}
			QNetworkRequest rq (qu);
			rq.setRawHeader ("User-Agent", ua);
			rq.setRawHeader ("Accept-Language", "en"); // else Qt sends the system locale
			rq.setAttribute (QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual); // no cookies sent or kept
			rq.setAttribute (QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
			if (off >= 0) rq.setRawHeader ("Range", QByteArray ("bytes=") + QByteArray::number ((qlonglong)off) + "-" + QByteArray::number ((qlonglong)(off + len - 1)));
			rq.setTransferTimeout (stall); // reset by every byte: a stall, not a total time
			QNetworkReply *r = nam->get (rq);
			struct S { bool over = false, fired = false; };
			auto s = std::make_shared<S>();
			QObject::connect (r, &QNetworkReply::downloadProgress, ctx, [r, most, s] (qint64 got, qint64 total) {
				if (s->over || (got <= most && total <= most)) return;
				s->over = true; // past the cap: aborted, and the request fails
				r->abort();
			});
			QTimer::singleShot (maxMs, r, [r, s] { // the total time cap, gone with the reply
				if (s->over || s->fired) return;
				s->over = true;
				r->abort();
			});
			QObject::connect (r, &QNetworkReply::finished, ctx, [r, done, off, len, most, s] {
				if (s->fired) return;
				s->fired = true;
				r->deleteLater();
				bool sent = false;
				try {
					QVariant sv = r->attribute (QNetworkRequest::HttpStatusCodeAttribute);
					int st = sv.isValid() ? sv.toInt() : 0;
					if (!sv.isValid()) {
						if (r->error() == QNetworkReply::NoError) st = 200;
						else if (r->error() == QNetworkReply::ContentNotFoundError) st = 404;
					} else if ((st == 200 || st == 206) && r->error() != QNetworkReply::NoError) st = 0; // cut off
					if (s->over || r->bytesAvailable() > most) st = 0;
					int ra = r->rawHeader ("Retry-After").toInt();
					bool part = off >= 0 && st == 206;
					QByteArray body = st == 200 || part ? r->readAll() : QByteArray();
					if (part) { // the asked range, or nothing
						st = RangeOk (r->rawHeader ("Content-Range"), off, len, body.size()) ? 200 : 0;
						if (!st) body.clear();
					} else if (off >= 0 && st == 200) body = body.mid (off, len); // the server sent the whole file, under the cap
					sent = true;
					done (st, body, ra);
				} catch (...) {
					if (!sent) try { done (0, QByteArray(), 0); } catch (...) {}
				}
			});
		} catch (...) {
			if (!called) try { done (0, QByteArray(), 0); } catch (...) {}
		}
	});
}
