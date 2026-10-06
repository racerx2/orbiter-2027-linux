
// custom: launcher skins; "Launchpad skin" entry in the Extra tab and its skin picker

#include "LauncherItem.h"
#include "LauncherSkin.h"
#include "LayoutSkin.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace {

	// custom: launcher layouts; what a skin brings, for the picker
	QString KindText (const custom::SkinManifest &m)
	{
		QStringList k;
		if (!m.qml.empty ()) k << "QML launcher";
		if (!m.qss.empty ()) k << "style sheet";
		if (!m.ui.empty ()) k << "layout (Qt Designer)";
		if (k.size () <= 1) return k.join (QString ());
		return k.mid (0, k.size () - 1).join (", ") + " and " + k.last ();
	}

	// custom: launcher layouts; name and colours for a new layout; false if cancelled
	bool AskNewLayout (QWidget *parent, custom::LauncherSkin *host, QString &name, QString &colours)
	{
		QDialog dlg (parent);
		dlg.setWindowTitle ("New layout");
		auto *lay = new QVBoxLayout (&dlg);
		auto *info = new QLabel ("Makes a skin folder with the stock Launchpad as Qt Designer forms, to edit in Qt Designer.", &dlg);
		info->setWordWrap (true);
		lay->addWidget (info);
		auto *form = new QFormLayout ();
		auto *edit = new QLineEdit ("My Launchpad", &dlg);
		edit->setMaxLength (64);
		auto *combo = new QComboBox (&dlg);
		combo->addItem ("None (the stock look)", QString ());
		for (const auto &m : host->Skins ())
			if (m.ok && !m.qss.empty ()) combo->addItem (QString::fromStdString (m.name), QString::fromStdString (m.id));
		form->addRow ("Name:", edit);
		form->addRow ("Copy the colours of:", combo);
		lay->addLayout (form);
		auto *buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
		QObject::connect (buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
		QObject::connect (buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
		lay->addWidget (buttons);
		edit->selectAll ();
		if (dlg.exec () != QDialog::Accepted) return false;
		name = edit->text ();
		colours = combo->currentData ().toString ();
		return true;
	}

}

char *custom::LauncherItem::Name ()
{
	static char name[] = "Launchpad skin";
	return name;
}

char *custom::LauncherItem::Description ()
{
	static char desc[] = "Choose how the Launchpad looks.\r\n\r\nSkins are folders in Orbiter's Skins directory: a style sheet for the classic Launchpad, a new launcher written in QML, or a layout of the classic Launchpad edited in Qt Designer (New layout...).\r\n\r\nCtrl+Shift+L in the Launchpad window returns to Classic.";
	return desc;
}

bool custom::LauncherItem::clbkOpen (QWidget *hLaunchpad)
{
	host->ScanSkins ();

	QDialog dlg (hLaunchpad);
	dlg.setWindowTitle ("Launchpad skin");
	dlg.resize (520, 440);
	auto *lay = new QVBoxLayout (&dlg);
	auto *list = new QListWidget (&dlg);
	auto *info = new QLabel (&dlg);
	info->setWordWrap (true);
	info->setTextFormat (Qt::PlainText);
	info->setAlignment (Qt::AlignTop | Qt::AlignLeft);
	info->setMinimumHeight (96);
	auto *buttons = new QDialogButtonBox (QDialogButtonBox::Apply | QDialogButtonBox::Close, &dlg);
	QPushButton *apply = buttons->button (QDialogButtonBox::Apply);
	auto *tools = new QHBoxLayout ();
	auto *newLayout = new QPushButton ("New layout...", &dlg);
	auto *designer = new QPushButton ("Open in Qt Designer", &dlg);
	newLayout->setAutoDefault (false);
	designer->setAutoDefault (false);
	tools->addWidget (newLayout);
	tools->addWidget (designer);
	tools->addStretch (1);
	lay->addWidget (new QLabel ("Skins found in the Skins folder:", &dlg));
	lay->addWidget (list, 1);
	lay->addWidget (info);
	lay->addLayout (tools);
	lay->addWidget (new QLabel ("Ctrl+Shift+L in the Launchpad window returns to Classic.", &dlg));
	lay->addWidget (buttons);

	const QString designerPath = LayoutSkin::DesignerPath ();
	auto note = [this](const QString &id) {
		const QString n = host->RestartNote (id);
		return (n.isEmpty () ? n : "\n\n" + n);
	};

	std::function<void (const QString &)> fill = [&](const QString &select) {
		const QString activeId = host->ActiveSkin ();
		list->clear ();
		auto addRow = [&](const QString &id, const QString &label, const QString &text, bool ok, const QString &uiDir) {
			auto *row = new QListWidgetItem (label + (id == activeId ? "  (active)" : ""), list);
			row->setData (Qt::UserRole, id);
			row->setData (Qt::UserRole + 1, text);
			row->setData (Qt::UserRole + 2, ok);
			row->setData (Qt::UserRole + 3, uiDir);
			if (!ok) row->setForeground (list->palette ().color (QPalette::Disabled, QPalette::Text));
			return row;
		};
		addRow (QString (), "Classic", "The original Launchpad, unchanged." + note (QString ()), true, QString ());
		std::vector<const SkinManifest*> sorted;
		for (const auto &m : host->Skins ()) sorted.push_back (&m);
		std::sort (sorted.begin (), sorted.end (), [](const SkinManifest *a, const SkinManifest *b) {
			return QString::fromStdString (a->name).compare (QString::fromStdString (b->name), Qt::CaseInsensitive) < 0;
		});
		for (const SkinManifest *m : sorted) {
			QString text = QString::fromStdString (m->name);
			if (!m->author.empty ()) text += " by " + QString::fromStdString (m->author);
			if (!m->version.empty ()) text += ", version " + QString::fromStdString (m->version);
			text += " (" + KindText (*m) + ", folder " + QString::fromStdString (m->id) + ")";
			if (!m->description.empty ()) text += "\n\n" + QString::fromStdString (m->description);
			if (!m->ok) text += "\n\nCan't be used: " + QString::fromStdString (m->reason);
			const QString id = QString::fromStdString (m->id);
			if (m->ok) text += note (id);
			QString uiDir;
			if (m->ok && !m->ui.empty ()) uiDir = QDir (QString::fromStdString (m->dir)).absoluteFilePath (QString::fromStdString (m->ui));
			addRow (id, QString::fromStdString (m->name), text, m->ok, uiDir);
		}
		int sel = 0;
		for (int i = 0; i < list->count (); i++)
			if (list->item (i)->data (Qt::UserRole).toString () == select) sel = i;
		list->setCurrentRow (sel);
	};

	QObject::connect (list, &QListWidget::currentItemChanged, &dlg, [info, apply, designer, designerPath](QListWidgetItem *row) {
		info->setText (row ? row->data (Qt::UserRole + 1).toString () : QString ());
		apply->setEnabled (row && row->data (Qt::UserRole + 2).toBool ());
		const QString uiDir = (row ? row->data (Qt::UserRole + 3).toString () : QString ());
		designer->setEnabled (!designerPath.isEmpty () && !uiDir.isEmpty () && QFileInfo (uiDir).isDir ());
		designer->setToolTip (designerPath.isEmpty () ? "Qt 6 Designer was not found (Ubuntu: qt6-tools-dev-tools)" : designerPath);
	});
	QObject::connect (apply, &QPushButton::clicked, &dlg, [this, list, &dlg]() {
		QListWidgetItem *row = list->currentItem ();
		if (!row || !row->data (Qt::UserRole + 2).toBool ()) return;
		host->RequestSkin (row->data (Qt::UserRole).toString ()); // runs once this dialog's loop has ended
		dlg.accept ();
	});
	QObject::connect (buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	QObject::connect (newLayout, &QPushButton::clicked, &dlg, [this, &dlg, &fill]() { // custom: launcher layouts
		QString name, colours, id, msg;
		if (!AskNewLayout (&dlg, host, name, colours)) return;
		QApplication::setOverrideCursor (Qt::WaitCursor);
		bool ok = LayoutSkin::Export (name, colours, id, msg);
		QApplication::restoreOverrideCursor ();
		if (ok) {
			host->ScanSkins ();
			fill (id);
			QMessageBox::information (&dlg, "New layout", msg);
		} else QMessageBox::warning (&dlg, "New layout", msg);
	});
	QObject::connect (designer, &QPushButton::clicked, &dlg, [list, &dlg, designerPath]() { // custom: launcher layouts
		QListWidgetItem *row = list->currentItem ();
		const QString uiDir = (row ? row->data (Qt::UserRole + 3).toString () : QString ());
		if (uiDir.isEmpty () || designerPath.isEmpty ()) return;
		QStringList files;
		QDir d (uiDir);
		for (const QString &f : QStringList {"IDD_MAIN.ui", "IDD_PAGE_SCN.ui", "IDD_PAGE_OPT.ui", "IDD_PAGE_MOD.ui", "IDD_PAGE_DEV.ui", "IDD_PAGE_EXT.ui", "IDD_PAGE_ABT.ui"})
			if (d.exists (f)) files << d.absoluteFilePath (f);
		if (!QProcess::startDetached (designerPath, files, uiDir))
			QMessageBox::warning (&dlg, "Open in Qt Designer", "Qt Designer could not be started: " + designerPath);
	});

	fill (host->StoredSkin ());
	dlg.exec ();
	return true;
}

int custom::LauncherItem::clbkWriteConfig ()
{
	return 0; // Launcher.cfg is written when something changes, not at every launch and exit
}
