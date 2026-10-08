// not upstream: the Launchpad's offer to download missing planet textures (TexPack.cpp does the work)

#include "TexInstall.h"
#include "TexPack.h"
#include "Config.h"
#include "Log.h"
#include "OrbiterAPI.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>
#include <filesystem>
#include <map>

static QString MB (uint64_t b)
{
	return QString::number (b / 1048576.0, 'f', b < 10485760 ? 1 : 0) + " MB";
}

bool TexInstallCheck (QWidget *parent, Config *cfg)
{
	if (!cfg->bPlanetTexCheck) return true;
	TexPackList list;
	std::string err;
	if (!TexPackRead (cfg->ConfigPath ("TexturePacks"), list, err)) {
		LOGOUT_WARN ("Planet texture packs: %s", err.c_str());
		return true;
	}
	char ptx[256];
	if (!cfg->PTexPath (ptx, sizeof ptx, "")) return true;
	std::string texdir = ptx;
	std::vector<TexPack> miss = TexPackMissing (list, texdir);
	if (miss.empty()) return true;

	std::vector<std::string> bodies; // TexturePacks.cfg order
	std::map<std::string, std::vector<const TexPack*>> by;
	for (const TexPack &p : miss) {
		if (!by.count (p.body)) bodies.push_back (p.body);
		by[p.body].push_back (&p);
	}

	QDialog dlg (parent);
	dlg.setWindowTitle ("Planet textures are missing");
	QVBoxLayout *lay = new QVBoxLayout (&dlg);
	lay->addWidget (new QLabel ("Surface textures are missing for these bodies.\n"
		"Download and install them from the Orbiter 2027 texture repository?", &dlg));
	QWidget *box = new QWidget;
	QVBoxLayout *bl = new QVBoxLayout (box);
	std::vector<std::pair<QCheckBox*, std::string>> checks;
	for (const std::string &b : bodies) {
		QStringList layers;
		uint64_t sz = 0;
		for (const TexPack *p : by[b]) { layers << QString::fromStdString (p->layer); sz += p->zipsize; }
		QCheckBox *c = new QCheckBox (QString ("%1: %2 (%3)").arg (QString::fromStdString (b), layers.join (", "), MB (sz)), box);
		c->setChecked (true);
		bl->addWidget (c);
		checks.push_back ({c, b});
	}
	bl->addStretch ();
	QScrollArea *sa = new QScrollArea (&dlg);
	sa->setWidget (box);
	sa->setWidgetResizable (true);
	lay->addWidget (sa);
	QLabel *info = new QLabel (&dlg);
	lay->addWidget (info);
	QCheckBox *noask = new QCheckBox ("Don't ask again", &dlg);
	lay->addWidget (noask);
	QDialogButtonBox *bb = new QDialogButtonBox (&dlg);
	QPushButton *dl = bb->addButton ("Download and install", QDialogButtonBox::AcceptRole);
	bb->addButton ("Not now", QDialogButtonBox::RejectRole);
	lay->addWidget (bb);
	QObject::connect (bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect (bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

	auto selected = [&]() {
		std::vector<const TexPack*> v;
		for (auto &c : checks)
			if (c.first->isChecked()) v.insert (v.end(), by[c.second].begin(), by[c.second].end());
		return v;
	};
	uint64_t need = 0, avail = 0; // disk: every unpacked layer plus the largest zip
	bool known = false;
	auto update = [&]() {
		uint64_t zs = 0, rs = 0, zmax = 0;
		for (const TexPack *p : selected()) { zs += p->zipsize; rs += p->rawsize; zmax = std::max (zmax, p->zipsize); }
		std::error_code ec;
		std::filesystem::space_info sp = std::filesystem::space (oapiResolvePath (texdir.c_str()), ec);
		need = rs + zmax;
		known = !ec;
		avail = known ? sp.available : 0;
		QString t = QString ("Download: %1, disk space needed: %2").arg (MB (zs), MB (need));
		if (known) t += QString (", free: %1").arg (MB (avail));
		info->setText (t);
		dl->setEnabled (zs > 0);
	};
	for (auto &c : checks) QObject::connect (c.first, &QCheckBox::toggled, update);
	update ();
	dlg.resize (460, 420);
	bool go = (dlg.exec() == QDialog::Accepted);
	if (noask->isChecked()) {
		cfg->bPlanetTexCheck = false;
		cfg->Write ();
	}
	if (!go) return true;

	std::vector<const TexPack*> todo = selected();
	std::string work = TexPackWorkDir (texdir);
	std::error_code ec;
	std::filesystem::create_directories (work, ec);
	if (ec) {
		QMessageBox::critical (parent, "Planet textures", QString ("Cannot write to the planet texture folder\n%1\n\n%2\n\n"
			"Set PlanetTexDir in Orbiter.cfg to a folder you can write to.").arg (QString::fromStdString (work), QString::fromStdString (ec.message())));
		return false;
	}
	std::filesystem::remove (work, ec);
	if (known && avail < need) {
		QMessageBox::critical (parent, "Planet textures", QString ("Not enough disk space for the selected textures.\n\nNeeded: %1\nFree: %2")
			.arg (MB (need), MB (avail)));
		return false;
	}

	uint64_t total = 0, base = 0;
	for (const TexPack *p : todo) total += p->zipsize + p->rawsize;
	QProgressDialog pd ("", "Cancel", 0, 1000, parent);
	pd.setWindowTitle ("Installing planet textures");
	pd.setWindowModality (Qt::WindowModal);
	pd.setMinimumDuration (0);
	pd.setAutoClose (false);
	pd.setAutoReset (false);
	pd.setMinimumWidth (420);
	pd.show ();
	QStringList failed;
	int n = 0, installed = 0;
	bool cancelled = false;
	for (const TexPack *p : todo) {
		n++;
		QString name = QString::fromStdString (p->zip);
		auto prog = [&](const char *what, uint64_t ofs, uint64_t done, uint64_t size) {
			pd.setLabelText (QString ("%1 %2 (%3 of %4)").arg (what, name).arg (n).arg (todo.size()));
			pd.setValue ((int)((base + ofs + std::min (done, size)) * 1000 / total));
			QCoreApplication::processEvents ();
			return !pd.wasCanceled ();
		};
		std::string perr;
		bool ok = TexPackInstall (list, *p, texdir,
			[&](uint64_t d) { return prog ("Downloading", 0, d, p->zipsize); },
			[&](uint64_t d) { return prog ("Installing", p->zipsize, d, p->rawsize); }, perr);
		base += p->zipsize + p->rawsize;
		if (ok) {
			installed++;
			LOGOUT ("Installed planet textures %s", p->zip.c_str());
			continue;
		}
		if (pd.wasCanceled ()) { cancelled = true; break; }
		failed << QString ("%1: %2").arg (name, QString::fromStdString (perr));
		LOGOUT_WARN ("Planet textures %s not installed: %s", p->zip.c_str(), perr.c_str());
	}
	pd.close ();
	if (cancelled) {
		QMessageBox::information (parent, "Planet textures", QString ("Installation cancelled.\n%1 of %2 packs were installed.")
			.arg (installed).arg (todo.size()));
		return false;
	}
	if (!failed.isEmpty())
		return QMessageBox::warning (parent, "Planet textures", "These packs could not be installed:\n\n" + failed.join ("\n") +
			"\n\nLaunch Orbiter anyway?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
	return true;
}
