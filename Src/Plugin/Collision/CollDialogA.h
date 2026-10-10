// not upstream: collision addon, the ImGui content of the "Collision damage" dialog (Design CA E3 9.3); E4 creates, owns and deletes the object
#ifndef COLLDIALOGA_H
#define COLLDIALOGA_H
#if COLL_HAVE_IMGUI
#include <string>
#include <vector>
#include "imgui.h"
#include "CollDamageA.h"

class CollDialogA : public ImGuiDialog {
public:
	typedef CollDmgSession *(*SessionFn) ();     // the running session's E3 part, read on every draw
	explicit CollDialogA (SessionFn fn) : ImGuiDialog ("Collision damage"), session (fn) {}
protected:
	void OnDraw () override // M9: an exception from the session or the Repair button stays in the addon
	{
		table = id = false;
		CollGuard ("dialog", [&] { Draw (); });
		if (id) ImGui::PopID ();
		if (table) ImGui::EndTable ();
	}
private:
	void Draw ()
	{
		CollDmgSession *s = session ? session () : nullptr;
		if (!s) { ImGui::TextUnformatted ("no session"); return; }
		std::vector<std::string> l;
		s->Report (l);
		if (!l.empty ()) ImGui::TextUnformatted (l[0].c_str ());
		if ((table = ImGui::BeginTable ("vessels", 7))) {
			ImGui::TableSetupColumn ("Vessel"); ImGui::TableSetupColumn ("Records"); ImGui::TableSetupColumn ("kJ");
			ImGui::TableSetupColumn ("Destroyed"); ImGui::TableSetupColumn ("Module"); ImGui::TableSetupColumn ("Cut"); ImGui::TableSetupColumn ("");
			ImGui::TableHeadersRow ();
			for (const auto &kv : s->Vessels ()) {
				const VesselDamageA &v = kv.second;
				ImGui::TableNextRow ();
				ImGui::TableNextColumn (); ImGui::TextUnformatted (v.name.c_str ());
				ImGui::TableNextColumn (); ImGui::Text ("%zu", v.d.rec.size ());
				ImGui::TableNextColumn (); ImGui::Text ("%.3g", v.d.eabs * 1e-3);
				ImGui::TableNextColumn (); ImGui::TextUnformatted ((v.d.flags & XDMG_DESTROYED) ? "yes" : "no");
				ImGui::TableNextColumn (); ImGui::TextUnformatted ((v.d.flags & XDMG_MODULEFX) ? "yes" : "no");
				ImGui::TableNextColumn (); ImGui::Text ("%s %u/%u", v.cut.dummy ? "on" : "off", v.cut.relinks, v.cut.remakes);
				ImGui::TableNextColumn ();
				ImGui::PushID ((int)kv.first);
				id = true;
				if (ImGui::SmallButton ("Repair")) { CollH h = s->VesselHandle (kv.first); if (h) s->RepairVessel (h); }
				ImGui::PopID ();
				id = false;
			}
			ImGui::EndTable ();
			table = false;
		}
		for (size_t i = 1; i < l.size (); i++) if (l[i].rfind ("  vessel", 0) != 0) ImGui::TextUnformatted (l[i].c_str ());
		if (s->QueuedRepairs ()) ImGui::Text ("repairs queued: %zu (next running frame)", s->QueuedRepairs ());
	}
	SessionFn session;
	bool table = false, id = false;              // ImGui scopes still open when Draw threw
};
#endif
#endif
