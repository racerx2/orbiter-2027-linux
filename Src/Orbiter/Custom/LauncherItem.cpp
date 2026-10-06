
// custom: launcher skins; "Launchpad skin" entry in the Extra tab and its skin picker

#include "LauncherItem.h"
#include "LauncherSkin.h"
#include "LayoutSkin.h"
#include "SkinCopy.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
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

	const char *SKIN_DIR = "Skins";

	// custom: launcher layouts; what a skin brings, for the picker
	QString KindText (const custom::SkinManifest &m)
	{
		QStringList k;
		if (!m.qml.empty ()) k << "QML launcher";
		if (!m.forms.empty ()) k << "Qt Designer launcher"; // custom: forms skins
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

	// custom: forms skins; the skins that come with Orbiter: an update replaces their files
	bool Shipped (const QString &id)
	{
		static const QStringList ids = {"Dark", "Horizon", "PlanetaryDefense"};
		return ids.contains (id);
	}

	// what Open in Qt Designer opens: a forms skin's form file, or a layout's ui folder; "" if nothing
	QString DesignerTarget (const custom::SkinManifest &m)
	{
		const QDir d (QString::fromStdString (m.dir));
		if (m.ok && !m.forms.empty ()) return d.absoluteFilePath (QString::fromStdString (m.forms)); // custom: forms skins; the launcher first
		if (m.ok && !m.ui.empty ()) return d.absoluteFilePath (QString::fromStdString (m.ui));
		return QString ();
	}

	// custom: forms skins; a message box whose text is never read as markup: it shows skin names
	void Tell (QWidget *parent, QMessageBox::Icon icon, const QString &title, const QString &text)
	{
		QMessageBox box (icon, title, text, QMessageBox::Ok, parent);
		box.setTextFormat (Qt::PlainText);
		box.exec ();
	}

	const custom::SkinManifest *FindSkin (custom::LauncherSkin *host, const QString &id)
	{
		for (const auto &m : host->Skins ())
			if (QString::fromStdString (m.id) == id) return &m;
		return nullptr;
	}

	// starts Qt Designer on a layout's forms or a forms skin's form, in its folder
	void OpenDesigner (QWidget *parent, const QString &designerPath, const QString &target)
	{
		const QFileInfo fi (target);
		QStringList files;
		QString cwd;
		if (fi.isFile ()) {
			files << fi.absoluteFilePath ();
			cwd = fi.absolutePath ();
		} else if (fi.isDir ()) {
			QDir d (target);
			for (const QString &f : QStringList {"IDD_MAIN.ui", "IDD_PAGE_SCN.ui", "IDD_PAGE_OPT.ui", "IDD_PAGE_MOD.ui", "IDD_PAGE_DEV.ui", "IDD_PAGE_EXT.ui", "IDD_PAGE_ABT.ui"})
				if (d.exists (f)) files << d.absoluteFilePath (f);
			cwd = d.absolutePath ();
		} else return;
		if (!QProcess::startDetached (designerPath, files, cwd))
			QMessageBox::warning (parent, "Open in Qt Designer", "Qt Designer could not be started: " + designerPath);
	}

	// custom: forms skins; the name for a copy; false if cancelled
	bool AskCopyName (QWidget *parent, const QString &from, QString &name)
	{
		QInputDialog d (parent);
		d.setWindowTitle ("Copy skin");
		d.setInputMode (QInputDialog::TextInput);
		d.setLabelText ("Name of the copy of '" + from + "':");
		d.setTextValue (("My " + from).left (custom::COPY_MAX_NAME));
		for (QLabel *l : d.findChildren<QLabel*> ()) l->setTextFormat (Qt::PlainText);
		if (QLineEdit *e = d.findChild<QLineEdit*> ()) e->setMaxLength (custom::COPY_MAX_NAME); // a longer folder name could fail
		if (d.exec () != QDialog::Accepted) return false;
		name = d.textValue ();
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
	static char desc[] = "Choose how the Launchpad looks.\r\n\r\nSkins are folders in Orbiter's Skins directory: a style sheet for the classic Launchpad, a new launcher made in Qt Designer (Horizon, Planetary Defense) or written in QML, or a layout of the classic Launchpad edited in Qt Designer (New layout...). Copy... makes your own copy of a skin to change.\r\n\r\nCtrl+Shift+L in the Launchpad window returns to Classic.";
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
	auto *copy = new QPushButton ("Copy...", &dlg);
	auto *designer = new QPushButton ("Open in Qt Designer", &dlg);
	newLayout->setAutoDefault (false);
	copy->setAutoDefault (false);
	designer->setAutoDefault (false);
	tools->addWidget (newLayout);
	tools->addWidget (copy);
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
			addRow (id, QString::fromStdString (m->name), text, m->ok, DesignerTarget (*m));
		}
		int sel = 0;
		for (int i = 0; i < list->count (); i++)
			if (list->item (i)->data (Qt::UserRole).toString () == select) sel = i;
		list->setCurrentRow (sel);
	};

	QObject::connect (list, &QListWidget::currentItemChanged, &dlg, [info, apply, copy, designer, designerPath](QListWidgetItem *row) {
		info->setText (row ? row->data (Qt::UserRole + 1).toString () : QString ());
		apply->setEnabled (row && row->data (Qt::UserRole + 2).toBool ());
		copy->setEnabled (row && !row->data (Qt::UserRole).toString ().isEmpty ()); // custom: forms skins; not Classic
		const QString target = (row ? row->data (Qt::UserRole + 3).toString () : QString ());
		designer->setEnabled (!designerPath.isEmpty () && !target.isEmpty () && QFileInfo::exists (target));
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
	// custom: forms skins; a copy under a new name, selected in the list; nullptr if cancelled or failed
	auto copySkin = [this, &dlg, &fill](const QString &id, bool ask) -> const SkinManifest * {
		const SkinManifest *m = FindSkin (host, id);
		if (!m) return nullptr;
		const QString from = QString::fromStdString (m->name);
		QString name = "My " + from;
		if (ask && !AskCopyName (&dlg, from, name)) return nullptr;
		QApplication::setOverrideCursor (Qt::WaitCursor);
		custom::SkinCopy c;
		const bool ok = custom::CopySkin (*m, QDir::current ().absoluteFilePath (SKIN_DIR), name, c);
		QApplication::restoreOverrideCursor ();
		host->ScanSkins (); // m is gone from here on
		fill (ok ? c.id : id);
		if (!ok) {
			Tell (&dlg, QMessageBox::Warning, "Copy skin", c.msg);
			return nullptr;
		}
		if (ask) Tell (&dlg, QMessageBox::Information, "Copy skin", c.msg + "\n\nIt is in this list now; Apply uses it.");
		return FindSkin (host, c.id);
	};
	QObject::connect (copy, &QPushButton::clicked, &dlg, [list, copySkin]() {
		QListWidgetItem *row = list->currentItem ();
		if (row && !row->data (Qt::UserRole).toString ().isEmpty ()) copySkin (row->data (Qt::UserRole).toString (), true);
	});
	QObject::connect (designer, &QPushButton::clicked, &dlg, [list, &dlg, designerPath, copySkin]() { // custom: launcher layouts
		QListWidgetItem *row = list->currentItem ();
		const QString target = (row ? row->data (Qt::UserRole + 3).toString () : QString ());
		if (target.isEmpty () || designerPath.isEmpty ()) return;
		const QString id = row->data (Qt::UserRole).toString ();
		if (!Shipped (id)) {
			OpenDesigner (&dlg, designerPath, target);
			return;
		}
		// custom: forms skins; an update of Orbiter replaces a shipped skin's files, and the changes with them
		QMessageBox ask (QMessageBox::Question, "Open in Qt Designer",
			"'" + row->text ().remove ("  (active)") + "' comes with Orbiter: an update replaces its files, and changes made to them.\n\n"
			"Make your own copy and open that?", QMessageBox::NoButton, &dlg);
		ask.setTextFormat (Qt::PlainText);
		QPushButton *mine = ask.addButton ("Make an editable copy", QMessageBox::AcceptRole);
		QPushButton *anyway = ask.addButton ("Open anyway", QMessageBox::DestructiveRole);
		ask.addButton (QMessageBox::Cancel);
		ask.setDefaultButton (mine);
		ask.exec ();
		if (ask.clickedButton () == anyway) OpenDesigner (&dlg, designerPath, target);
		else if (ask.clickedButton () == mine) {
			const SkinManifest *c = copySkin (id, false);
			if (!c) return;
			const QString name = QString::fromStdString (c->name);
			OpenDesigner (&dlg, designerPath, DesignerTarget (*c));
			Tell (&dlg, QMessageBox::Information, "Open in Qt Designer", "Qt Designer opens your copy, '" + name + "', which is selected in this list now.\n\n"
				"Apply it to see your changes in the launcher. While it is the active skin, the launcher builds the form again each time you save.");
		}
	});

	fill (host->StoredSkin ());
	dlg.exec ();
	return true;
}

int custom::LauncherItem::clbkWriteConfig ()
{
	return 0; // Launcher.cfg is written when something changes, not at every launch and exit
}
