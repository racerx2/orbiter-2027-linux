// not upstream: the Launchpad's offer to download the SPICE kernels (TexPack.cpp does the work)

#include "KernelInstall.h"
#include "TexPack.h"
#include "Config.h"
#include "Log.h"
#include "OrbiterAPI.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

static const char *KERNELDIR = "Kernels";

bool KernelsInstalled (Config *cfg)
{
	KernelPackList list;
	std::string err;
	if (!KernelPackRead (cfg->ConfigPath ("KernelPacks"), list, err) || list.file.empty()) return false;
	std::error_code ec;
	if (!fs::is_regular_file (oapiResolvePath ("Modules/Celbody/spice.so"), ec)) return false;
	for (const TexPack &k : list.file) if (!KernelPackPresent (KERNELDIR, k)) return false;
	return true;
}

static QString MB (uint64_t b)
{
	if (b >= 1073741824) return QString::number (b / 1073741824.0, 'f', 2) + " GB";
	return QString::number (b / 1048576.0, 'f', b < 10485760 ? 1 : 0) + " MB";
}

bool KernelInstallCheck (QWidget *parent, Config *cfg)
{
	KernelPackList list;
	std::string err;
	if (!KernelPackRead (cfg->ConfigPath ("KernelPacks"), list, err)) {
		LOGOUT_WARN ("SPICE kernels: %s", err.c_str());
		cfg->bSpice = false;
		return true;
	}
	std::vector<const TexPack*> miss;
	for (const TexPack &k : list.file) if (!KernelPackPresent (KERNELDIR, k)) miss.push_back (&k);
	if (miss.empty() || !cfg->bKernelCheck) {
		cfg->bSpice = KernelsInstalled (cfg);
		return true;
	}

	uint64_t dl = 0, spare = 0;
	std::vector<uint64_t> kept;
	for (const TexPack *k : miss) {
		kept.push_back (KernelPackResume (KERNELDIR, *k));
		dl += k->rawsize - kept.back();
		for (const TexPart &t : k->parts) spare = std::max (spare, t.size);
	}
	uint64_t need = dl + spare; // the joined files plus the part being downloaded
	std::error_code ec;
	std::string dir = oapiResolvePath (KERNELDIR);
	fs::create_directories (dir, ec);
	fs::space_info sp = fs::space (dir, ec);
	bool known = !ec;

	QDialog dlg (parent);
	dlg.setWindowTitle ("SPICE ephemeris kernels");
	QVBoxLayout *lay = new QVBoxLayout (&dlg);
	lay->addWidget (new QLabel ("Orbiter can place the planets and moons from JPL's SPICE ephemeris kernels (1900 to 2199).\n"
		"They are not installed, so the built-in series are used.\n\n"
		"Download and install the kernels from the Orbiter 2027 texture repository?", &dlg));
	QString t = QString ("Download: %1, disk space needed: %2").arg (MB (dl), MB (need));
	if (known) t += QString (", free: %1").arg (MB (sp.available));
	lay->addWidget (new QLabel (t, &dlg));
	QCheckBox *noask = new QCheckBox ("Don't ask again", &dlg);
	lay->addWidget (noask);
	QDialogButtonBox *bb = new QDialogButtonBox (&dlg);
	bb->addButton ("Download and install", QDialogButtonBox::AcceptRole);
	bb->addButton ("Not now", QDialogButtonBox::RejectRole);
	lay->addWidget (bb);
	QObject::connect (bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect (bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	bool go = (dlg.exec() == QDialog::Accepted);
	if (noask->isChecked()) {
		cfg->bKernelCheck = false;
		cfg->Write ();
	}
	if (!go) {
		cfg->bSpice = KernelsInstalled (cfg);
		return true;
	}
	if (known && sp.available < need) {
		QMessageBox::critical (parent, "SPICE kernels", QString ("Not enough disk space for the kernels.\n\nNeeded: %1\nFree: %2").arg (MB (need), MB (sp.available)));
		return false;
	}

	QProgressDialog pd ("", "Cancel", 0, 1000, parent);
	pd.setWindowTitle ("Installing SPICE kernels");
	pd.setWindowModality (Qt::WindowModal);
	pd.setMinimumDuration (0);
	pd.setAutoClose (false);
	pd.setAutoReset (false);
	pd.setMinimumWidth (420);
	pd.show ();
	QStringList failed;
	uint64_t base = 0, total = std::max<uint64_t> (dl, 1);
	bool cancelled = false;
	for (size_t i = 0; i < miss.size(); i++) {
		const TexPack *k = miss[i];
		QString name = QString::fromStdString (k->zip);
		uint64_t k0 = kept[i];
		std::string perr;
		bool ok = KernelPackInstall (list, *k, KERNELDIR, [&](uint64_t d) {
				uint64_t done = d > k0 ? d - k0 : 0;
				pd.setLabelText (QString ("Downloading %1 (%2 of %3)\n%4 of %5").arg (name).arg (i + 1).arg (miss.size()).arg (MB (d), MB (k->rawsize)));
				pd.setValue ((int)((base + std::min (done, k->rawsize - k0)) * 1000 / total));
				QCoreApplication::processEvents ();
				return !pd.wasCanceled ();
			}, perr);
		base += k->rawsize - k0;
		if (ok) {
			LOGOUT ("Installed SPICE kernel %s", k->zip.c_str());
			continue;
		}
		if (pd.wasCanceled ()) { cancelled = true; break; }
		failed << QString ("%1: %2").arg (name, QString::fromStdString (perr));
		LOGOUT_WARN ("SPICE kernel %s not installed: %s", k->zip.c_str(), perr.c_str());
	}
	pd.close ();
	cfg->bSpice = KernelsInstalled (cfg);
	if (cancelled) {
		QMessageBox::information (parent, "SPICE kernels", "Installation cancelled.\nA cancelled download carries on where it stopped next time.");
		return false;
	}
	if (!failed.isEmpty())
		return QMessageBox::warning (parent, "SPICE kernels", "These kernels could not be installed:\n\n" + failed.join ("\n") +
			"\n\nLaunch Orbiter with the built-in series?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
	return true;
}
