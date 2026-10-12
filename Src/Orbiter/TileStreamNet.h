// not upstream: HTTP GETs for the tile stream on a Qt thread of its own (TileStream.cpp)

#ifndef __TILESTREAMNET_H
#define __TILESTREAMNET_H

#include <QByteArray>
#include <functional>
#include <string>

class QNetworkAccessManager;
class QObject;
class QThread;

class TsNet {
public:
	// status: HTTP status, 200 for a file:// read, 404 for a missing file, 0 when nothing answered
	using Reply = std::function<void (int status, QByteArray body, int retryAfter)>;

	explicit TsNet (const std::string &userAgent);
	~TsNet ();
	void Get (const std::string &url, Reply done);

private:
	QThread *thread;
	QObject *ctx;
	QNetworkAccessManager *nam;
	QByteArray ua;
};

#endif // !__TILESTREAMNET_H
