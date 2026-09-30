// not upstream: WM_POWERBROADCAST counterpart, logind's PrepareForSleep signal on the system bus

#ifndef __SLEEPWATCH_H
#define __SLEEPWATCH_H

#include <QObject>

class SleepWatch : public QObject {
	Q_OBJECT
public:
	SleepWatch (QObject *parent = nullptr);
private slots:
	void PrepareForSleep (bool start);
private:
	bool frozen = false; // this watch froze the running simulation for the sleep
};

#endif // !__SLEEPWATCH_H
