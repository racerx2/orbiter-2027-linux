// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// ======================================================================
// Help window
// ======================================================================
#include "DlgHelp.h"
#include "ChmHelp.h"
#include "imgui.h"

// This is just a placeholder to call the HtmlHelp API
// We draw nothing ourselves
DlgHelp::DlgHelp():ImGuiDialog("Orbiter: Help") {}
void DlgHelp::Display() {}
void DlgHelp::OnDraw() {}

void DlgHelp::OpenHelp(const HELPCONTEXT *hc)
{
	char buf[256];
	// the help window is a top-level window of its own (the render window is a QWindow, not a widget)
	if(hc->topic)
		snprintf(buf, 256, "%s::%s", hc->helpfile, hc->topic);
	else
		snprintf(buf, 256, "%s", hc->helpfile);

	buf[255] = '\0';

	if(!HtmlHelp (NULL, buf, NULL)) {
		oapiAddNotification(OAPINOTIF_ERROR, "Failed to open help", buf);
	}
}
