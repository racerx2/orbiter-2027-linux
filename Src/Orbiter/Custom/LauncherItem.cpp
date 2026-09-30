// custom: launcher skins; "Launchpad skin" entry in the Extra tab and its skin picker

#include "LauncherItem.h"
#include "LauncherSkin.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

char *custom::LauncherItem::Name ()
{
	static char name[] = "Launchpad skin";
	return name;
}

char *custom::LauncherItem::Description ()
{
	static char desc[] = "Choose how the Launchpad looks.\r\n\r\nSkins are folders in Orbiter's Skins directory: a style sheet for the classic Launchpad, or a new launcher written in QML.\r\n\r\nCtrl+Shift+L in the Launchpad window returns to Classic.";
	return desc;
}

bool custom::LauncherItem::clbkOpen (QWidget *hLaunchpad)
{
	host->ScanSkins ();

	QDialog dlg (hLaunchpad);
	dlg.setWindowTitle ("Launchpad skin");
	dlg.resize (480, 400);
	auto *lay = new QVBoxLayout (&dlg);
	auto *list = new QListWidget (&dlg);
	auto *info = new QLabel (&dlg);
	info->setWordWrap (true);
	info->setTextFormat (Qt::PlainText);
	info->setAlignment (Qt::AlignTop | Qt::AlignLeft);
	info->setMinimumHeight (96);
	auto *buttons = new QDialogButtonBox (QDialogButtonBox::Apply | QDialogButtonBox::Close, &dlg);
	QPushButton *apply = buttons->button (QDialogButtonBox::Apply);
	lay->addWidget (new QLabel ("Skins found in the Skins folder:", &dlg));
	lay->addWidget (list, 1);
	lay->addWidget (info);
	lay->addWidget (new QLabel ("Ctrl+Shift+L in the Launchpad window returns to Classic.", &dlg));
	lay->addWidget (buttons);

	const QString activeId = host->ActiveSkin ();
	auto addRow = [&](const QString &id, const QString &label, const QString &text, bool ok) {
		auto *row = new QListWidgetItem (label + (id == activeId ? "  (active)" : ""), list);
		row->setData (Qt::UserRole, id);
		row->setData (Qt::UserRole + 1, text);
		row->setData (Qt::UserRole + 2, ok);
		if (!ok) row->setForeground (list->palette ().color (QPalette::Disabled, QPalette::Text));
		return row;
	};

	addRow (QString (), "Classic", "The original Launchpad, unchanged.", true);
	std::vector<const SkinManifest*> sorted;
	for (const auto &m : host->Skins ()) sorted.push_back (&m);
	std::sort (sorted.begin (), sorted.end (), [](const SkinManifest *a, const SkinManifest *b) {
		return QString::fromStdString (a->name).compare (QString::fromStdString (b->name), Qt::CaseInsensitive) < 0;
	});
	for (const SkinManifest *m : sorted) {
		QString kind = (!m->qml.empty () && !m->qss.empty () ? "QML launcher and style sheet" : !m->qml.empty () ? "QML launcher" : "style sheet");
		QString text = QString::fromStdString (m->name);
		if (!m->author.empty ()) text += " by " + QString::fromStdString (m->author);
		if (!m->version.empty ()) text += ", version " + QString::fromStdString (m->version);
		text += " (" + kind + ", folder " + QString::fromStdString (m->id) + ")";
		if (!m->description.empty ()) text += "\n\n" + QString::fromStdString (m->description);
		if (!m->ok) text += "\n\nCan't be used: " + QString::fromStdString (m->reason);
		addRow (QString::fromStdString (m->id), QString::fromStdString (m->name), text, m->ok);
	}

	QObject::connect (list, &QListWidget::currentItemChanged, &dlg, [info, apply](QListWidgetItem *row) {
		info->setText (row ? row->data (Qt::UserRole + 1).toString () : QString ());
		apply->setEnabled (row && row->data (Qt::UserRole + 2).toBool ());
	});
	QObject::connect (apply, &QPushButton::clicked, &dlg, [this, list, &dlg]() {
		QListWidgetItem *row = list->currentItem ();
		if (!row || !row->data (Qt::UserRole + 2).toBool ()) return;
		host->RequestSkin (row->data (Qt::UserRole).toString ()); // runs once this dialog's loop has ended
		dlg.accept ();
	});
	QObject::connect (buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

	const QString stored = host->StoredSkin ();
	int sel = 0;
	for (int i = 0; i < list->count (); i++)
		if (list->item (i)->data (Qt::UserRole).toString () == stored) sel = i;
	list->setCurrentRow (sel);

	dlg.exec ();
	return true;
}

int custom::LauncherItem::clbkWriteConfig ()
{
	return 0; // Launcher.cfg is written when something changes, not at every launch and exit
}
