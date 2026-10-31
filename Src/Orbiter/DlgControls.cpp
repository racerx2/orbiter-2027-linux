// not upstream (controls-editor): the Controllers page of the in-sim Options dialog: the controllers, their names and
// calibration, and the bindings of every profile, edited as on the Launchpad's page (TabControls.cpp)

#include "DlgOptions.h"
#include "Orbiter.h"
#include "Vessel.h"
#include "InputEditor.h"
#include "imgui.h"
#include "imgui_extras.h"
#include <cstring>
#include <memory>

extern Orbiter* g_pOrbiter;
extern Vessel* g_focusobj;

namespace {

enum Page { P_FUNC, P_KEY, P_AXIS, P_MFD, P_LAYER, P_NONE };

struct Ui {
	std::unique_ptr<InputEditor> ed;
	std::string dev;                 // the controller selected
	int row = -1;                    // the binding selected
	int lastFrame = -100;
	std::string msg;                 // the last edit's error
	std::string info;                // what the last reset did
	// reset to defaults
	bool rProf = true, rVess = false, rKeys = false;
	size_t rCount = 0;               // vessel class profiles
	// rename
	char name[40] = "";
	std::string renameErr;
	// calibration
	InputCalibrator cal;
	std::string calDev;
	bool calDone = false;
	// the binding being edited
	Binding b;
	int index = -1;                  // -1: a new one
	bool have = false, vessel = false;
	int page = P_FUNC;
	std::unique_ptr<InputCaptureFilter> cap, keycap;
	char funcFilter[64] = "", profFilter[64] = "";
};
Ui ui;

void Reread ()
{
	g_pOrbiter->OnOptionChanged (OPTCAT_JOYSTICK, OPTITEM_JOYSTICK_DEVICE);
	if (ui.ed) ui.ed->Open (ui.ed->Name());
}

void Stop (InputSystem *inp) // the page went away or a popup ended: no capture, nothing held back
{
	ui.cap.reset ();
	ui.keycap.reset ();
	inp->HoldBack (false);
}

const char *modes[] = { "Normal (held: held; others once)", "Held while pressed", "Once on press", "Once on release",
	"Once on press and once on release", "Toggle: press on, press off", "Repeat while held" };
const char *views[] = { "Any view", "Cockpit view only", "Outside view only" };
const char *types[] = { "An Orbiter function", "A key or key combination", "An analog control", "An MFD button", "A shift layer", "Nothing" };
const char *amodes[] = { "Value while held", "Step by", "Set to" };

std::string MfdName (int m) { return m == 0 ? "Left MFD" : m == 1 ? "Right MFD" : "MFD " + std::to_string (m+1); }
std::string MfdBtnName (int b) { return b == IMFD_PWR ? "PWR" : b == IMFD_SEL ? "SEL" : b == IMFD_MNU ? "MNU" : "Button " + std::to_string (b+1); }

Binding Collect ()
{
	Binding b = ui.b;
	bool axis = (b.in.kind == INK_AXIS);
	if (!axis) b.in.zone = false;
	if (axis && !b.in.zone) b.mode = IMODE_AUTO;
	if (b.in.src == INSRC_DEV) b.pass = false;
	switch (ui.page) {
	case P_FUNC: b.out = IOUT_FUNC; break;
	case P_KEY: b.out = IOUT_KEY; break;
	case P_AXIS:
		b.out = IOUT_AXIS;
		if (b.chan < 0) b.chan = 0;
		if (axis && !b.in.zone) b.amode = IAX_VALUE;
		else b.invert = b.half = b.pickup = false, b.dz = 0, b.sat = b.curve = b.scale = 1;
		if (!InputChannel (b.chan).unipolar) b.half = false;
		if (!InputChannel (b.chan).absolute) b.pickup = false;
		if (!InputChannel (b.chan).absolute && !(axis && !b.in.zone)) b.amode = IAX_VALUE;
		break;
	case P_MFD: b.out = IOUT_MFD; break;
	case P_LAYER: b.out = IOUT_LAYER; if (b.lnum < 1) b.lnum = 1; break;
	default: b.out = IOUT_NONE; break;
	}
	return b;
}

void StartEdit (InputSystem *inp, const Binding &init, int index, bool have)
{
	ui.b = init;
	ui.index = index;
	ui.have = have;
	ui.info.clear ();
	ui.vessel = ui.ed && strcasecmp (ui.ed->Name().c_str(), "Global");
	ui.page = init.out == IOUT_KEY ? P_KEY : init.out == IOUT_AXIS ? P_AXIS : init.out == IOUT_MFD ? P_MFD :
		init.out == IOUT_LAYER ? P_LAYER : init.out == IOUT_NONE && have ? P_NONE : P_FUNC;
	ui.funcFilter[0] = '\0';
	ui.keycap.reset ();
	ui.cap.reset (have ? nullptr : new InputCaptureFilter (inp, InputCaptureFilter::INPUT));
	ImGui::OpenPopup ("Binding##controls");
}

void DrawBinding (InputSystem *inp)
{
	ImGui::SetNextWindowSize (ImVec2 (620, 0), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal ("Binding##controls", nullptr, ImGuiWindowFlags_None)) return;
	InputRef r;
	if (ui.cap && inp->Captured (r)) {
		ui.cap.reset ();
		ui.b.in = r;
		ui.have = true;
		if (r.kind == INK_AXIS) ui.page = P_AXIS; // a whole axis drives an analog control
	}
	int dik;
	uint16_t mods;
	if (ui.keycap && ui.keycap->KeyGot (dik, mods)) ui.keycap.reset (), ui.b.key = dik, ui.b.kmods = mods;

	ImGui::SeparatorText ("Control");
	Binding shown;
	shown.in = ui.b.in;
	ImGui::TextUnformatted (ui.cap ? "Waiting: press a button, move an axis through half its travel, push the hat, turn a dial, press a key, "
		"or a mouse button (left and right with Shift, Ctrl or Alt)" : ui.have ? InputEditor::InputText (shown).c_str() : "No control yet");
	if (ImGui::Button (ui.cap ? "Cancel waiting" : "Press a control...")) {
		if (ui.cap) ui.cap.reset ();
		else ui.keycap.reset (), ui.cap.reset (new InputCaptureFilter (inp, InputCaptureFilter::INPUT));
	}
	bool axis = ui.have && ui.b.in.kind == INK_AXIS;
	if (axis) {
		bool valid;
		double x = inp->Engine().AxisValue (ui.b.in.dev, ui.b.in.code, &valid);
		ImGui::SameLine ();
		if (valid) ImGui::Text ("Now: %.3f", x);
		else ImGui::TextUnformatted ("Not connected");
	}
	if (ui.have) {
		static const char *layers[] = { "Base (no shift layer)", "Shift layer 1", "Shift layer 2", "Shift layer 3", "Shift layer 4",
			"Shift layer 5", "Shift layer 6", "Shift layer 7", "Shift layer 8" };
		ImGui::Combo ("Layer", &ui.b.layer, layers, 9);
		if (axis) {
			ImGui::Checkbox ("Part of the axis works as a button", &ui.b.in.zone);
			if (ui.b.in.zone) {
				if (ui.b.in.zlo == 0 && ui.b.in.zhi == 0) ui.b.in.zlo = 0.9, ui.b.in.zhi = 1.0;
				double lo = -1, hi = 1;
				ImGui::SliderScalar ("From", ImGuiDataType_Double, &ui.b.in.zlo, &lo, &hi, "%.2f");
				ImGui::SliderScalar ("To", ImGuiDataType_Double, &ui.b.in.zhi, &lo, &hi, "%.2f");
			}
		}
		if (!axis || ui.b.in.zone) {
			int m = ui.b.mode;
			if (ImGui::Combo ("Acts", &m, modes, 7)) ui.b.mode = (InputMode)m;
			if (ui.b.mode == IMODE_REPEAT) {
				double lo = 0.5, hi = 100;
				ImGui::SliderScalar ("Per second", ImGuiDataType_Double, &ui.b.rate, &lo, &hi, "%.1f");
			}
		}
		ImGui::Combo ("View", &ui.b.view, views, 3);
		if (ui.b.in.src != INSRC_DEV) ImGui::Checkbox ("The key or button also keeps doing what it does without this binding", &ui.b.pass);
	}

	ImGui::SeparatorText ("Does");
	if (ui.vessel) types[P_NONE] = "Nothing (takes this control's Global binding away)";
	else types[P_NONE] = "Nothing";
	ImGui::Combo ("##type", &ui.page, types, 6);
	switch (ui.page) {
	case P_FUNC: {
		ImGui::InputTextWithHint ("##ff", "Search", ui.funcFilter, sizeof ui.funcFilter);
		if (ImGui::BeginListBox ("##funcs", ImVec2 (-1, 180))) {
			for (int f = 0; f < LKEY_COUNT; f++) {
				char key[64], line[160];
				g_pOrbiter->keymap.KeyText (f, key);
				snprintf (line, sizeof line, key[0] ? "%s   (key %s)" : "%s", Keymap::FuncName (f), key);
				if (ui.funcFilter[0] && !strcasestr (line, ui.funcFilter)) continue;
				if (ImGui::Selectable (line, ui.b.out == IOUT_FUNC && ui.b.func == f)) ui.b.out = IOUT_FUNC, ui.b.func = f;
			}
			ImGui::EndListBox ();
		}
		break;
	}
	case P_KEY: {
		std::string kn = InputKeyName (ui.b.key);
		if (ImGui::BeginCombo ("Key", kn.empty() ? "Pick a key" : kn.c_str())) {
			for (int k = 1; k < 256; k++) {
				std::string n = InputKeyName (k);
				if (!n.empty() && ImGui::Selectable (n.c_str(), k == ui.b.key)) ui.b.key = k;
			}
			ImGui::EndCombo ();
		}
		bool s = ui.b.kmods & KMOD_SHIFT, c = ui.b.kmods & KMOD_CTRL, a = ui.b.kmods & KMOD_ALT;
		ImGui::Checkbox ("Shift", &s); ImGui::SameLine ();
		ImGui::Checkbox ("Ctrl", &c); ImGui::SameLine ();
		ImGui::Checkbox ("Alt", &a); ImGui::SameLine ();
		ui.b.kmods = (s ? KMOD_SHIFT : 0) | (c ? KMOD_CTRL : 0) | (a ? KMOD_ALT : 0);
		if (ImGui::Button (ui.keycap ? "Waiting for the key..." : "Press the key..."))
			ui.cap.reset (), ui.keycap.reset (new InputCaptureFilter (inp, InputCaptureFilter::KEY));
		break;
	}
	case P_AXIS: {
		if (ui.b.chan < 0) ui.b.chan = 0;
		if (ImGui::BeginCombo ("Control##chan", InputChannel (ui.b.chan).name.c_str())) {
			for (int c = 0; c < ICH_COUNT; c++)
				if (ImGui::Selectable (InputChannel (c).name.c_str(), c == ui.b.chan)) ui.b.chan = c;
			ImGui::EndCombo ();
		}
		ImGui::SetItemTooltip ("Pitch, Yaw, Bank, TransX/Y/Z: attitude and translation; Main, Retro, Hover (and single engines): "
			"throttles; ElevTrim, RudderTrim, Flaps; BrakeL/R, Brakes; LookX/Y, PanX/Y, FOV, Distance: views");
		const InputChanInfo &ci = InputChannel (ui.b.chan);
		if (axis && !ui.b.in.zone) {
			ImGui::Checkbox ("Invert", &ui.b.invert);
			if (ci.unipolar) { ImGui::SameLine (); ImGui::Checkbox ("Centre to end only", &ui.b.half); }
			if (ci.absolute) { ImGui::SameLine (); ImGui::Checkbox ("Wait until moved (pickup)", &ui.b.pickup); }
			double z = 0, dzmax = 0.95, smin = 0.05, one = 1, cmin = 0.1, cmax = 10, smn = -10, smx = 10;
			ImGui::SliderScalar ("Dead zone", ImGuiDataType_Double, &ui.b.dz, &z, &dzmax, "%.3f");
			ImGui::SliderScalar ("Saturation", ImGuiDataType_Double, &ui.b.sat, &smin, &one, "%.3f");
			ImGui::SliderScalar ("Curve", ImGuiDataType_Double, &ui.b.curve, &cmin, &cmax, "%.2f");
			ImGui::SliderScalar ("Scale", ImGuiDataType_Double, &ui.b.scale, &smn, &smx, "%.2f");
			bool valid;
			double x = inp->Engine().AxisValue (ui.b.in.dev, ui.b.in.code, &valid);
			if (valid) ImGui::Text ("Gives: %.3f", inp->Engine().Process (Collect (), x));
		} else {
			int am = ui.b.amode;
			ImGui::BeginDisabled (!ci.absolute);
			if (ImGui::Combo ("##amode", &am, amodes, 3)) ui.b.amode = (InputAxMode)am;
			ImGui::EndDisabled ();
			ImGui::SameLine ();
			double lo = -1, hi = 1;
			ImGui::SliderScalar ("##aval", ImGuiDataType_Double, &ui.b.aval, &lo, &hi, "%.3f");
		}
		break;
	}
	case P_MFD: {
		if (ImGui::BeginCombo ("MFD", MfdName (ui.b.mfd).c_str())) {
			for (int m = 0; m < MAXMFD; m++) if (ImGui::Selectable (MfdName (m).c_str(), m == ui.b.mfd)) ui.b.mfd = m;
			ImGui::EndCombo ();
		}
		if (ImGui::BeginCombo ("Button", MfdBtnName (ui.b.mfdbtn).c_str())) {
			for (int k = IMFD_MNU; k < 20; k++) if (ImGui::Selectable (MfdBtnName (k).c_str(), k == ui.b.mfdbtn)) ui.b.mfdbtn = k;
			ImGui::EndCombo ();
		}
		break;
	}
	case P_LAYER:
		if (ui.b.lnum < 1) ui.b.lnum = 1;
		ImGui::SliderInt ("Layer##lnum", &ui.b.lnum, 1, 8);
		ImGui::Checkbox ("Latch: press on, press off (otherwise while held)", &ui.b.latch);
		break;
	default:
		break;
	}

	ImGui::Separator ();
	Binding b = Collect ();
	std::string err;
	if (!ui.have) err = "Press a control first.";
	else if (b.out == IOUT_FUNC && b.func < 0) err = "Pick a function.";
	bool ok = err.empty() && InputEditor::Check (b, err);
	if (ok) ImGui::TextWrapped ("In the file: BIND %s", InputFormatBinding (b).c_str());
	else ImGui::TextColored (ImVec4 (0.9f, 0.4f, 0.4f, 1.0f), "%s", err.c_str());
	ImGui::BeginDisabled (!ok);
	if (ImGui::Button ("OK", ImVec2 (100, 0))) {
		Stop (inp);
		if (ui.ed->Put (ui.index, b, ui.msg)) ui.msg.clear ();
		Reread ();
		ImGui::CloseCurrentPopup ();
	}
	ImGui::EndDisabled ();
	ImGui::SameLine ();
	if (ImGui::Button ("Cancel", ImVec2 (100, 0))) {
		Stop (inp);
		ImGui::CloseCurrentPopup ();
	}
	ImGui::EndPopup ();
}

void DrawCalibration (InputSystem *inp)
{
	ImGui::SetNextWindowSize (ImVec2 (560, 0), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal ("Calibrate##controls", nullptr, ImGuiWindowFlags_None)) return;
	inp->HoldBack (true); // moving the stick here doesn't fly the vessel
	const InputDevice *d = InputEditor::Device (inp, ui.calDev);
	if (!d) ImGui::TextUnformatted ("The controller was unplugged.");
	else if (!ui.calDone) {
		ui.cal.Update (*d);
		ImGui::TextWrapped ("Move every axis of %s through its whole travel a few times: stick and pedals to both ends, levers and "
			"toe brakes from end to end. Then let go of everything and press Done moving.", ui.calDev.c_str());
	} else ImGui::TextWrapped ("Check the kind of each axis, then Save. A centring axis keeps where it rests as its centre; "
		"a lever's centre is the middle of its travel.");
	if (ImGui::BeginTable ("cal", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupColumn ("Axis"), ImGui::TableSetupColumn ("Seen"), ImGui::TableSetupColumn ("Rests at"), ImGui::TableSetupColumn ("Kind");
		ImGui::TableHeadersRow ();
		for (auto &a : ui.cal.axes) {
			ImGui::PushID (a.code);
			ImGui::TableNextRow ();
			ImGui::TableNextColumn (); ImGui::TextUnformatted (InputAbsName (a.code).c_str());
			ImGui::TableNextColumn (); ImGui::Text ("%d to %d (of %d to %d)", a.lo, a.hi, a.min, a.max);
			ImGui::TableNextColumn ();
			const InputAxis *ax = d ? d->Axis (a.code) : nullptr;
			if (ui.calDone) ImGui::Text ("%d", a.rest);
			else if (ax) ImGui::Text ("now %d", ax->value);
			ImGui::TableNextColumn ();
			if (!ui.calDone || !ui.cal.Moved (a)) ImGui::TextUnformatted (ui.calDone ? "Not moved" : "");
			else {
				int k = a.centring ? 0 : 1;
				static const char *kinds[] = { "Centring (stick, rudder)", "Lever (throttle, toe brake)" };
				ImGui::SetNextItemWidth (-1);
				if (ImGui::Combo ("##kind", &k, kinds, 2)) a.centring = (k == 0);
			}
			ImGui::PopID ();
		}
		ImGui::EndTable ();
	}
	bool close = false;
	ImGui::BeginDisabled (ui.calDone || !d);
	if (ImGui::Button ("Done moving")) { ui.cal.Finish (*d); ui.calDone = true; }
	ImGui::EndDisabled ();
	ImGui::SameLine ();
	ImGui::BeginDisabled (!ui.calDone || ui.cal.Result().empty());
	if (ImGui::Button ("Save")) {
		if (ui.ed->WriteCalibration (ui.calDev, ui.cal.Result(), ui.msg)) ui.msg.clear ();
		close = true;
	}
	ImGui::EndDisabled ();
	ImGui::SameLine ();
	if (ImGui::Button ("Reset to the raw range")) {
		if (ui.ed->WriteCalibration (ui.calDev, {}, ui.msg)) ui.msg.clear ();
		close = true;
	}
	ImGui::SetItemTooltip ("Removes this controller's calibration: each axis uses the whole range the device reports");
	ImGui::SameLine ();
	if (ImGui::Button ("Cancel")) { Stop (inp); ImGui::CloseCurrentPopup (); }
	if (close) { Stop (inp); Reread (); ImGui::CloseCurrentPopup (); }
	ImGui::EndPopup ();
}

void DrawRename (InputSystem *)
{
	if (!ImGui::BeginPopupModal ("Rename controller##controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
	ImGui::TextUnformatted ("New name (letters, digits, _ and -); the bindings change with it:");
	ImGui::InputText ("##name", ui.name, sizeof ui.name);
	if (!ui.renameErr.empty()) ImGui::TextColored (ImVec4 (0.9f, 0.4f, 0.4f, 1.0f), "%s", ui.renameErr.c_str());
	if (ImGui::Button ("OK", ImVec2 (100, 0))) {
		if (!strcasecmp (ui.name, ui.dev.c_str()) || ui.ed->Rename (ui.dev, ui.name, ui.renameErr)) {
			ui.dev = ui.name;
			ui.renameErr.clear ();
			Reread ();
			ImGui::CloseCurrentPopup ();
		}
	}
	ImGui::SameLine ();
	if (ImGui::Button ("Cancel", ImVec2 (100, 0))) { ui.renameErr.clear (); ImGui::CloseCurrentPopup (); }
	ImGui::EndPopup ();
}

void DrawReset ()
{
	if (!ImGui::BeginPopupModal ("Reset to defaults##controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
	bool global = !strcasecmp (ui.ed->Name().c_str(), "Global");
	std::string cls = ui.ed->Name().substr (ui.ed->Name().find ('/') + 1);
	ImGui::TextUnformatted ("What goes back to Orbiter's defaults:");
	ImGui::Checkbox (global ? "All vessels (Global): the bindings Orbiter first made from Orbiter.cfg's joystick settings"
		: ("Vessel class " + cls + ": its own bindings removed, so those for all vessels apply").c_str(), &ui.rProf);
	if (ui.rCount) ImGui::Checkbox (("Every vessel class's own bindings (" + std::to_string (ui.rCount) + " classes)").c_str(), &ui.rVess);
	ImGui::Checkbox ("Keyboard keys (keymap.cfg): Orbiter's default keys", &ui.rKeys);
	ImGui::TextDisabled ("The old files are kept next to them as .bak. Controller names and calibration stay.");
	bool vess = ui.rVess && ui.rCount;
	ImGui::BeginDisabled (!ui.rProf && !vess && !ui.rKeys);
	if (ImGui::Button ("Reset", ImVec2 (100, 0))) {
		std::vector<std::string> done;
		std::string note, err;
		bool ok = true;
		if (ui.rProf && (ok = ui.ed->Reset (note, err))) done.push_back (global ? "all vessels (Global)" : "vessel class " + cls);
		if (ok && vess && (ok = ui.ed->ResetVessels (err))) done.push_back ("every vessel class (" + std::to_string (ui.rCount) + ")");
		if (ok && ui.rKeys && (ok = InputEditor::ResetKeymap (g_pOrbiter->keymap, oapiResolvePath ("keymap.cfg"), err))) done.push_back ("the keyboard keys");
		Reread ();
		ui.row = -1;
		ui.msg = ok ? std::string() : err;
		ui.info.clear ();
		for (size_t i = 0; i < done.size(); i++) ui.info += (i ? ", " : "Back to the defaults: ") + done[i];
		if (!done.empty()) ui.info += ". The old files are kept as .bak.";
		if (!note.empty()) ui.info += (ui.info.empty() ? "" : "\n") + note;
		ImGui::CloseCurrentPopup ();
	}
	ImGui::EndDisabled ();
	ImGui::SameLine ();
	if (ImGui::Button ("Cancel", ImVec2 (100, 0))) ImGui::CloseCurrentPopup ();
	ImGui::EndPopup ();
}

} // namespace

void DlgOptions::DrawControls() // not upstream (input-bindings, controls-editor): was DrawJoystick, the one joystick's settings
{
	InputSystem *inp = g_pOrbiter->GetInput();
	if (!inp) return;
	int frame = ImGui::GetFrameCount();
	if (frame - ui.lastFrame > 2) Stop (inp); // the page wasn't shown: nothing of an old popup goes on
	ui.lastFrame = frame;
	inp->Touch ();
	std::string own = g_focusobj ? "Vessels/" + InputClassPath (g_focusobj->ClassName()) : std::string ("Global");
	if (!ui.ed) {
		ui.ed.reset (new InputEditor (inp, InputEditor::VesselDir (inp)));
		ui.ed->Open (own); // this vessel first
	}

	// the controllers
	ImGui::SeparatorText ("Controllers");
	bool any = false, selFound = false;
	if (ImGui::BeginTable ("devs", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn ("Name"), ImGui::TableSetupColumn ("Controller"), ImGui::TableSetupColumn ("State");
		ImGui::TableHeadersRow ();
		for (auto &d : inp->Devices().Devices()) {
			if (d->state == InputDevice::GONE) continue;
			any = true;
			bool open = (d->state == InputDevice::OPEN);
			std::string name = open ? inp->NameOf (d->id) : std::string();
			if (ui.dev.empty() && open) ui.dev = name;
			bool sel = open && !strcasecmp (name.c_str(), ui.dev.c_str());
			selFound |= sel;
			ImGui::PushID (d->id);
			ImGui::TableNextRow ();
			ImGui::TableNextColumn ();
			if (ImGui::Selectable (open ? name.c_str() : "-", sel, ImGuiSelectableFlags_SpanAllColumns) && open) ui.dev = name;
			ImGui::TableNextColumn ();
			ImGui::Text ("%s (%d buttons, %d axes%s)", d->name.c_str(), (int)d->buttons.size(), (int)d->axes.size(), d->hatPresent[0] ? ", hat" : "");
			ImGui::TableNextColumn ();
			ImGui::TextUnformatted (open ? "Connected" : "No access");
			if (!open) ImGui::SetItemTooltip ("%s", InputAccessHint (*d).c_str());
			ImGui::PopID ();
		}
		ImGui::EndTable ();
	}
	if (!any) ImGui::TextUnformatted ("No controllers found. Plug one in: it shows here at once.");
	if (!selFound) ui.dev.clear ();
	const InputDevice *dv = ui.dev.empty() ? nullptr : InputEditor::Device (inp, ui.dev);
	ImGui::BeginDisabled (!dv);
	if (ImGui::Button ("Rename...")) {
		snprintf (ui.name, sizeof ui.name, "%s", ui.dev.c_str());
		ImGui::OpenPopup ("Rename controller##controls");
	}
	ImGui::SameLine ();
	if (ImGui::Button ("Calibrate...") && dv) {
		ui.calDev = ui.dev, ui.calDone = false;
		ui.cal.Start (*dv);
		inp->HoldBack (true);
		ImGui::OpenPopup ("Calibrate##controls");
	}
	ImGui::EndDisabled ();
	if (dv) { // live values
		for (size_t i = 0; i < dv->axes.size(); i++) {
			double v = inp->Engine().AxisValue (ui.dev, dv->axes[i].code);
			char buf[32];
			snprintf (buf, sizeof buf, "%s %.2f", InputAbsName (dv->axes[i].code).c_str(), v);
			if (i % 2) ImGui::SameLine ();
			ImGui::ProgressBar ((float)(0.5 * (v + 1.0)), ImVec2 (ImGui::GetContentRegionAvail().x * (i % 2 ? 1.0f : 0.5f) - (i % 2 ? 0 : 4), 0), buf);
		}
		std::string down;
		for (size_t i = 0; i < dv->btn.size() && i < dv->buttons.size(); i++) if (dv->btn[i]) down += " B" + std::to_string (i+1);
		static const char *hd[] = {"UP", "RIGHT", "DOWN", "LEFT"};
		for (int h = 0; h < 4; h++) {
			if (!dv->hatPresent[h]) continue;
			int x = dv->hat[h][0], y = dv->hat[h][1];
			int dir = y < 0 ? 0 : x > 0 ? 1 : y > 0 ? 2 : x < 0 ? 3 : -1;
			if (dir >= 0) down += " HAT" + std::to_string (h) + "." + hd[dir];
		}
		ImGui::Text ("Pressed now:%s", down.empty() ? " nothing" : down.c_str());
	}
	ImGui::Checkbox ("Controllers work while Orbiter is in the background", &g_pOrbiter->Cfg()->CfgJoystickPrm.bBackground);
	int joy = g_pOrbiter->Cfg()->CfgJoystickPrm.bOff ? 0 : 1; // not upstream: the old Joystick tab's device list (input-bindings)
	if (ImGui::Combo ("Joystick device", &joy, "<Disabled>\0<All controllers>\0")) g_pOrbiter->Cfg()->CfgJoystickPrm.bOff = (joy == 0);

	// the bindings
	ImGui::SeparatorText ("Bindings");
	auto label = [&](const std::string &p) {
		if (!strcasecmp (p.c_str(), "Global")) return std::string ("All vessels (Global)");
		std::string c = p.substr (p.find ('/') + 1);
		return (!strcasecmp (p.c_str(), own.c_str()) ? "This vessel's class: " : "Vessel class ") + c;
	};
	if (ImGui::BeginCombo ("For", label (ui.ed->Name()).c_str(), ImGuiComboFlags_HeightLarge)) {
		ImGui::InputTextWithHint ("##pf", "Search", ui.profFilter, sizeof ui.profFilter);
		auto item = [&](const std::string &p) {
			std::string l = label (p);
			if (ui.profFilter[0] && !strcasestr (l.c_str(), ui.profFilter)) return;
			if (ImGui::Selectable (l.c_str(), p == ui.ed->Name()) && p != ui.ed->Name()) ui.ed->Open (p), ui.row = -1, ui.info.clear ();
		};
		item ("Global");
		if (own != "Global") item (own);
		for (auto &c : ui.ed->Classes()) if ("Vessels/" + c != own) item ("Vessels/" + c);
		ImGui::EndCombo ();
	}
	const InputProfile &p = ui.ed->Profile();
	std::vector<Binding> inh = ui.ed->Inherited();
	int nown = (int)p.binds.size(), ntot = nown + (int)inh.size();
	if (ui.row >= ntot) ui.row = -1;
	bool edit = false;
	if (ImGui::BeginTable ("binds", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2 (0, 220))) {
		ImGui::TableSetupScrollFreeze (0, 1);
		ImGui::TableSetupColumn ("Control"), ImGui::TableSetupColumn ("Does");
		ImGui::TableHeadersRow ();
		for (int i = 0; i < ntot; i++) {
			const Binding &b = (i < nown ? p.binds[i] : inh[i - nown]);
			ImGui::PushID (i);
			ImGui::TableNextRow ();
			ImGui::TableNextColumn ();
			std::string c = (i < nown ? ui.ed->Conflict (b, i, &g_pOrbiter->keymap) : std::string());
			std::string also = (i < nown ? ui.ed->AlsoDoes (b, i) : std::string());
			std::string in = (c.empty() ? "" : "(!) ") + InputEditor::InputText (b);
			if (!also.empty()) c += (c.empty() ? "" : "\n") + also;
			if (i >= nown) ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
			if (ImGui::Selectable (in.c_str(), ui.row == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
				ui.row = i;
				if (ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left)) edit = true;
			}
			if (!c.empty()) ImGui::SetItemTooltip ("%s", c.c_str());
			else if (i >= nown) ImGui::SetItemTooltip ("For all vessels. Change... makes this vessel class's own binding for the control.");
			ImGui::TableNextColumn ();
			ImGui::Text ("%s%s", InputEditor::OutputText (b).c_str(), i >= nown ? "   (from Global)" : "");
			if (i >= nown) ImGui::PopStyleColor ();
			ImGui::PopID ();
		}
		ImGui::EndTable ();
	}
	if (ImGui::Button ("Add...")) StartEdit (inp, Binding(), -1, false);
	ImGui::SetItemTooltip ("Press a button, move an axis or push the hat, then pick what it does");
	ImGui::SameLine ();
	ImGui::BeginDisabled (ui.row < 0);
	if (ImGui::Button ("Change...") || (edit && ui.row >= 0)) {
		if (ui.row < nown) StartEdit (inp, p.binds[ui.row], ui.row, true);
		else { // a Global binding: this class gets its own
			Binding b = inh[ui.row - nown];
			b.note.clear (), b.tail.clear (), b.line = 0;
			StartEdit (inp, b, -1, true);
		}
	}
	ImGui::EndDisabled ();
	ImGui::SameLine ();
	ImGui::BeginDisabled (ui.row < 0 || ui.row >= nown);
	if (ImGui::Button ("Remove")) {
		if (ui.ed->Remove (ui.row, ui.msg)) ui.msg.clear ();
		Reread ();
		ui.row = -1;
		ui.info.clear ();
	}
	ImGui::EndDisabled ();
	ImGui::SameLine ();
	if (ImGui::Button ("Reset...")) {
		ui.rProf = true, ui.rVess = ui.rKeys = false;
		ui.rCount = ui.ed->VesselProfiles().size();
		ImGui::OpenPopup ("Reset to defaults##controls");
	}
	ImGui::SetItemTooltip ("Back to Orbiter's defaults: these bindings, every vessel class's, or the keyboard keys");
	for (auto &e : p.errors) ImGui::TextColored (ImVec4 (0.9f, 0.4f, 0.4f, 1.0f), "%s", e.c_str());
	if (!ui.msg.empty()) ImGui::TextColored (ImVec4 (0.9f, 0.4f, 0.4f, 1.0f), "%s", ui.msg.c_str());
	if (!ui.info.empty()) ImGui::TextWrapped ("%s", ui.info.c_str());
	else if (ui.msg.empty()) ImGui::TextDisabled ("Saved to %s.cfg at once; Orbiter uses it at once.", ui.ed->Name().c_str());

	DrawBinding (inp);
	DrawCalibration (inp);
	DrawRename (inp);
	DrawReset ();
}
