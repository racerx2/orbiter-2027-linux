// not upstream: HTTP GETs for the tile stream on a Qt thread of its own (TileStreamNet.h)

#include "TileStreamNet.h"
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QUrl>

TsNet::TsNet (const std::string &userAgent)
: nam(nullptr), ua(QByteArray::fromStdString (userAgent))
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

void TsNet::Get (const std::string &url, Reply done, int64_t off, int64_t len)
{
	QMetaObject::invokeMethod (ctx, [this, url, done, off, len] {
		if (!nam) return;
		QUrl qu (QString::fromStdString (url));
		if (off >= 0 && qu.isLocalFile()) { // a range of a local file, read here
			QFile f (qu.toLocalFile());
			if (!f.open (QIODevice::ReadOnly)) { done (404, QByteArray(), 0); return; }
			if (off >= f.size() || !f.seek (off)) { done (416, QByteArray(), 0); return; }
			done (200, f.read (len), 0);
			return;
		}
		QNetworkRequest rq (qu);
		rq.setRawHeader ("User-Agent", ua);
		if (off >= 0) rq.setRawHeader ("Range", QByteArray ("bytes=") + QByteArray::number ((qlonglong)off) + "-" + QByteArray::number ((qlonglong)(off + len - 1)));
		rq.setTransferTimeout (30000);
		QNetworkReply *r = nam->get (rq);
		QObject::connect (r, &QNetworkReply::finished, ctx, [r, done, off, len] {
			QVariant sv = r->attribute (QNetworkRequest::HttpStatusCodeAttribute);
			int st = sv.isValid() ? sv.toInt() : 0;
			if (!sv.isValid()) {
				if (r->error() == QNetworkReply::NoError) st = 200;
				else if (r->error() == QNetworkReply::ContentNotFoundError) st = 404;
			} else if ((st == 200 || st == 206) && r->error() != QNetworkReply::NoError) st = 0; // cut off
			int ra = r->rawHeader ("Retry-After").toInt();
			bool part = off >= 0 && st == 206;
			if (part) st = 200;
			QByteArray body = st == 200 ? r->readAll() : QByteArray();
			if (off >= 0 && st == 200 && !part) body = body.mid (off, len); // the server sent the whole file
			r->deleteLater();
			done (st, body, ra);
		});
	});
}
