
// not upstream: HTTP GETs for the tile stream on a Qt thread of its own (TileStream.cpp)

#ifndef __TILESTREAMNET_H
#define __TILESTREAMNET_H

#include <QByteArray>
#include <cstdint>
#include <functional>
#include <string>

class QNetworkAccessManager;
class QObject;
class QThread;

class TsNet {
public:
	// status: HTTP status, 200 for a file:// read, 404 for a missing file, 0 when nothing answered, it was cut off or too large
	using Reply = std::function<void (int status, QByteArray body, int retryAfter)>;

	// cap: largest answer (cloud TIFF 36 MB); rangeCap: largest range answer; totalMs: longest a request may take
	explicit TsNet (const std::string &userAgent, int64_t cap = (int64_t)96 << 20, int64_t rangeCap = (int64_t)8 << 20, int totalMs = TotalMs);
	~TsNet ();
	void Get (const std::string &url, Reply done, int64_t off = -1, int64_t len = 0, int maxMs = 0); // off >= 0: those bytes only; maxMs > 0: own total cap

	static const int StallMs = 30000;        // no byte for this long: the request fails
	static const int TotalMs = 15 * 60000;   // a request still running after this fails, however slowly it moves
	static const int LongMs = 3 * 3600000;   // the same for a request waited on until stop (the cloud TIFF)

private:
	QThread *thread;
	QObject *ctx;
	QNetworkAccessManager *nam;
	QByteArray ua;
	int64_t cap, rangeCap;
	int totalMs;
};

#endif // !__TILESTREAMNET_H
