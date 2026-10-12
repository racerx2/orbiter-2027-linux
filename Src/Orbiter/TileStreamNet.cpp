// not upstream: HTTP GETs for the tile stream on a Qt thread of its own (TileStreamNet.h)

#include "TileStreamNet.h"
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

void TsNet::Get (const std::string &url, Reply done)
{
	QMetaObject::invokeMethod (ctx, [this, url, done] {
		if (!nam) return;
		QNetworkRequest rq (QUrl (QString::fromStdString (url)));
		rq.setRawHeader ("User-Agent", ua);
		rq.setTransferTimeout (30000);
		QNetworkReply *r = nam->get (rq);
		QObject::connect (r, &QNetworkReply::finished, ctx, [r, done] {
			QVariant sv = r->attribute (QNetworkRequest::HttpStatusCodeAttribute);
			int st = sv.isValid() ? sv.toInt() : 0;
			if (!sv.isValid()) {
				if (r->error() == QNetworkReply::NoError) st = 200;
				else if (r->error() == QNetworkReply::ContentNotFoundError) st = 404;
			} else if (st == 200 && r->error() != QNetworkReply::NoError) st = 0; // cut off
			int ra = r->rawHeader ("Retry-After").toInt();
			QByteArray body = st == 200 ? r->readAll() : QByteArray();
			r->deleteLater();
			done (st, body, ra);
		});
	});
}
