// not upstream (head-track): the Launchpad Controllers page's Head tracking group, its live view and its settings dialog

#ifndef __HEADTRACKUI_H
#define __HEADTRACKUI_H

#include <QGroupBox>
#include <string>

class QCheckBox;
class QLabel;
class QPushButton;
class HeadTrackView;

class HeadTrackBox : public QGroupBox {
public:
	explicit HeadTrackBox (QWidget *parent);
	~HeadTrackBox ();
	bool Tick (bool shown); // the page's timer; true: the first TrackIR got its keys, the bindings changed
	void Keys ();           // the bindings changed: the keys line again

private:
	void Settings ();
	QCheckBox *m_on;
	QLabel *m_status, *m_keys, *m_pose;
	QPushButton *m_access;
	QWidget *m_live;
	HeadTrackView *m_view;
	bool m_using = false;
	std::string m_text;
};

#endif // !__HEADTRACKUI_H
