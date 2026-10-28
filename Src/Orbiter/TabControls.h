// not upstream (input-bindings, controls-editor): Launchpad Options page "Controllers": the controllers found, their
// names and calibration, and the bindings of every profile

#ifndef __TABCONTROLS_H
#define __TABCONTROLS_H

#include "OptionsPages.h"
#include <QPointer>
#include <memory>
#include <string>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;
class QWidget;
class InputEditor;

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
	void FillDevices();
	void FillLive();
	void FillProfiles();
	void FillBindings();
	void Reload();
	void Rename();
	void Calibrate();
	void Edit(bool add);
	void Remove();
	void ResetDefaults();
	std::string SelectedDevice() const;

	QWidget *m_page = nullptr;
	QTableWidget *m_devs = nullptr, *m_binds = nullptr;
	QWidget *m_live = nullptr;
	QGridLayout *m_liveGrid = nullptr;
	QLabel *m_liveButtons = nullptr, *m_status = nullptr;
	QComboBox *m_prof = nullptr;
	QCheckBox *m_background = nullptr;
	QPushButton *m_rename = nullptr, *m_calib = nullptr, *m_change = nullptr, *m_remove = nullptr;
	QPointer<QTimer> m_timer;
	std::unique_ptr<InputEditor> m_ed;
	std::string m_devKey;   // the device rows shown, to fill the table only when it changes
	std::string m_liveDev;  // the device whose live controls are shown
	QObject m_life; // the controls' connections end with the page, not with its window
};

#endif // !__TABCONTROLS_H
