// not upstream: the SPICE kernels (Config/KernelPacks.cfg): the installed check and the Launchpad's offer to download them

#ifndef __KERNELINSTALL_H
#define __KERNELINSTALL_H

class Config;
class QWidget;

// spice.so and every kernel of KernelPacks.cfg in Kernels/ at its size
bool KernelsInstalled (Config *cfg);

// offers missing kernels, installs them and sets cfg->bSpice; false cancels the launch
bool KernelInstallCheck (QWidget *parent, Config *cfg);

#endif // !__KERNELINSTALL_H
