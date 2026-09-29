#ifndef __HTMLCTRL_H
#define __HTMLCTRL_H

#include "OrbiterPlatform.h"

void RegisterHtmlCtrl (void *hInstance, BOOL active = true);
long DisplayHTMLPage(QWidget *hwnd, const char *webPageName);
long DisplayHTMLStr(QWidget *hwnd, const char *string);

#endif // !__HTMLCTRL_H
