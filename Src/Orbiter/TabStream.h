
// not upstream: Launchpad Options page "Earth streaming": tile stream settings, cache size and clearing, region prefetch

#ifndef __TABSTREAM_H
#define __TABSTREAM_H

#include "OptionsPages.h"
#include "TileStream.h"
#include <memory>
#include <thread>
#include <QPointer>
#include <QTimer>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTimer;

class OptionsPage_Stream : public OptionsPage {
public:
	OptionsPage_Stream(OptionsPageContainer* container);
	~OptionsPage_Stream();
	int ResourceId() const { return -1; }
	const char* Name() const { return "Earth streaming"; }
	void UpdateControls(QWidget *hPage);
	void UpdateConfig(QWidget *hPage);
	static void StopPrefetch(); // the Launchpad hides: a session is starting its own stream

protected:
	BOOL OnInitDialog(QWidget *hPage);

private:
	struct Size { std::atomic<bool> ready {false}; uint64_t streamed = 0, total = 0; size_t tiles = 0; long cleared = -1; };
	struct Fetch { std::atomic<bool> cancel {false}, finished {false}; std::atomic<int> result {-1}; TileStream::Progress p; };
	std::string BodyDir() const;
	std::string CfgFile() const;
	void LoadBases();
	void ScanSize();
	void Tick();
	void Estimate();
	void OnClear();
	void OnDownload();
	void Cancel();
	void Join();

	QCheckBox *m_enable = nullptr, *m_offline = nullptr, *m_base = nullptr;
	QSpinBox *m_cap = nullptr, *m_level = nullptr;
	QLabel *m_size = nullptr, *m_estimate = nullptr, *m_status = nullptr, *m_credits = nullptr;
	QPushButton *m_clear = nullptr, *m_refresh = nullptr, *m_download = nullptr, *m_cancel = nullptr;
	QComboBox *m_where = nullptr;
	QDoubleSpinBox *m_lat = nullptr, *m_lng = nullptr, *m_radius = nullptr;
	QProgressBar *m_bar = nullptr;
	QPointer<QTimer> m_timer;
	std::thread m_sizeThread, m_fetchThread;
	std::shared_ptr<Size> m_sz;
	std::shared_ptr<Fetch> m_fetch;
	int m_lmax = 16, m_elmax = 0; // last levels the cfg's SURF and ELEV sources serve
};

#endif // !__TABSTREAM_H
