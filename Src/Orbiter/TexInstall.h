// not upstream: the Launchpad's offer to download missing planet textures

#ifndef __TEXINSTALL_H
#define __TEXINSTALL_H

class QWidget;
class Config;

// before a launch: offers the missing packs; false keeps the Launchpad open
bool TexInstallCheck (QWidget *parent, Config *cfg);

#endif // !__TEXINSTALL_H
