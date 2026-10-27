// not upstream (input-bindings): Launchpad Options page "Controllers": the controllers found, and where their bindings are

#ifndef __TABCONTROLS_H
#define __TABCONTROLS_H

#include "OptionsPages.h"
#include <QPointer>

class QCheckBox;
class QLabel;
class QTimer;

class OptionsPage_Controls : public OptionsPage {
public:
	OptionsPage_Controls(OptionsPageContainer* container);
	~OptionsPage_Controls();
	int ResourceId() const { return -1; }
	const char* Name() const { return "Controllers"; }
	const HELPCONTEXT* HelpContext() const;
	void UpdateControls(QWidget *hPage);

protected:
	BOOL OnInitDialog(QWidget *hPage);

private:
	void Tick();

	QLabel *m_list = nullptr, *m_where = nullptr;
	QCheckBox *m_background = nullptr;
	QPointer<QTimer> m_timer;
	QObject m_life; // the controls' connections end with the page, not with its window
};

#endif // !__TABCONTROLS_H
