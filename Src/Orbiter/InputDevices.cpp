// not upstream: game controllers from evdev, every device at once, found in sysfs, hot-plugged (input-bindings)

#include "InputDevices.h"
#include <algorithm>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <linux/input.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

static_assert (INPUTDEV_EV_CNT == EV_CNT && INPUTDEV_KEY_CNT == KEY_CNT && INPUTDEV_ABS_CNT == ABS_CNT &&
	INPUTDEV_REL_CNT == REL_CNT && INPUTDEV_PROP_CNT == INPUT_PROP_CNT, "evdev counts");

// ====================================================================================
// sysfs capabilities and classification

bool InputParseBits (const std::string &text, size_t nbits, std::vector<bool> &bits)
{
	std::vector<std::string> words;
	size_t p = 0;
	while (p < text.size()) {
		while (p < text.size() && isspace ((unsigned char)text[p])) p++;
		size_t q = p;
		while (q < text.size() && isxdigit ((unsigned char)text[q])) q++;
		if (q == p) {
			if (p < text.size()) return false;
			break;
		}
		words.push_back (text.substr (p, q-p));
		p = q;
	}
	if (words.empty()) return false;
	const size_t wbits = 8*sizeof(long); // the kernel's unsigned long words, unpadded, leading zero words left out
	bits.assign (nbits, false);
	for (size_t i = 0; i < words.size(); i++) {
		unsigned long long w = strtoull (words[i].c_str(), nullptr, 16);
		size_t base = (words.size()-1-i)*wbits;
		for (size_t b = 0; b < wbits; b++)
			if ((w >> b) & 1ull && base+b < nbits) bits[base+b] = true;
	}
	return true;
}

bool InputIsJoystick (const InputCaps &c)
{
	bool has_keys = c.ev[EV_KEY];
	bool has_abs = c.abs[ABS_X] && c.abs[ABS_Y];
	bool has_3d = has_abs && c.abs[ABS_Z];
	if (c.prop[INPUT_PROP_ACCELEROMETER] || (!has_keys && has_3d)) return false;

	bool has_stylus = c.key[BTN_STYLUS], has_pen = c.key[BTN_TOOL_PEN];
	bool finger_no_pen = c.key[BTN_TOOL_FINGER] && !c.key[BTN_TOOL_PEN];
	bool has_mouse_button = false;
	for (int b = BTN_MOUSE; b < BTN_JOYSTICK && !has_mouse_button; b++) has_mouse_button = c.key[b];
	bool is_direct = c.prop[INPUT_PROP_DIRECT], has_touch = c.key[BTN_TOUCH];
	bool has_wheel = c.rel[REL_WHEEL] || c.rel[REL_HWHEEL];
	bool has_pad_buttons = c.key[BTN_0] && has_stylus && !has_pen;

	size_t nbtn = 0, naxis = 0;
	if (!c.key[BTN_JOYSTICK-1]) { // a mouse with more than 16 buttons runs into the joystick range
		for (int b = BTN_JOYSTICK; b < BTN_DIGI; b++) if (c.key[b]) nbtn++;
		for (int b = BTN_TRIGGER_HAPPY1; b <= BTN_TRIGGER_HAPPY40; b++) if (c.key[b]) nbtn++;
		for (int b = BTN_DPAD_UP; b <= BTN_DPAD_RIGHT; b++) if (c.key[b]) nbtn++;
	}
	for (int a = ABS_RX; a < ABS_PRESSURE; a++) if (c.abs[a]) naxis++;

	bool joy = false;
	if (has_abs) {
		if (has_stylus || has_pen || (finger_no_pen && !is_direct) || has_mouse_button || has_touch || is_direct) joy = false;
		else joy = (nbtn > 0 || naxis > 0);
	} else joy = (nbtn > 0 || naxis > 0);
	if (!joy) return false;

	static const int kbdkeys[] = { KEY_LEFTCTRL, KEY_CAPSLOCK, KEY_NUMLOCK, KEY_INSERT, KEY_MUTE,
		KEY_CALC, KEY_FILE, KEY_MAIL, KEY_PLAYPAUSE, KEY_BRIGHTNESSDOWN };
	size_t nkbd = 0;
	if (has_keys) for (int k : kbdkeys) if (c.key[k]) nkbd++;
	if (nkbd >= 4 || nbtn + naxis < 2) return false;
	if (has_wheel && has_pad_buttons) return false;
	return true;
}

bool InputIsController (const InputCaps &c)
{
	if (InputIsJoystick (c)) return true;
	if (c.prop[INPUT_PROP_ACCELEROMETER] || c.prop[INPUT_PROP_DIRECT] || c.prop[INPUT_PROP_POINTER] || c.rel[REL_X] || c.rel[REL_Y]) return false;
	bool box = c.rel[REL_DIAL] || c.rel[REL_MISC];
	for (int k = 1; k < KEY_CNT; k++) {
		if (!c.key[k]) continue;
		bool button = (k >= BTN_MISC && k < BTN_MOUSE) || (k >= BTN_JOYSTICK && k < BTN_DIGI) || (k >= BTN_WHEEL && k <= BTN_GEAR_UP) ||
			(k >= BTN_DPAD_UP && k <= BTN_DPAD_RIGHT) || (k >= BTN_TRIGGER_HAPPY1 && k <= BTN_TRIGGER_HAPPY40);
		if (!button) return false; // keyboard, media, mouse or pen keys
		if (k <= BTN_9) box = true;
	}
	int naxis = 0;
	for (int a = ABS_X; a <= ABS_BRAKE; a++) if (c.abs[a]) naxis++;
	for (int a = ABS_HAT0X; a <= ABS_HAT3Y; a++) if (c.abs[a]) naxis++;
	return box || naxis >= 2; // button boxes and dials; pedals and levers with no buttons (udev wants two of RX to HAT3Y)
}

bool InputIsLegacyJoystick (const InputCaps &c)
{
	return c.ev[EV_ABS] && c.abs[ABS_X] &&
		(c.key[BTN_JOYSTICK] || c.key[BTN_GAMEPAD] || c.key[BTN_TRIGGER_HAPPY1]);
}

// ====================================================================================
// InputDevice

int InputDevice::EventNumber () const
{
	return (sysname.compare (0, 5, "event") ? -1 : atoi (sysname.c_str()+5));
}

std::string InputDevice::Iface () const
{
	size_t p = phys.rfind ('/');
	return (p == std::string::npos ? std::string() : phys.substr (p+1));
}

std::string InputDevice::Port () const
{
	size_t p = phys.rfind ('/');
	return (p == std::string::npos ? phys : phys.substr (0, p));
}

int InputDevice::ButtonIndex (int code) const
{
	for (size_t i = 0; i < buttons.size(); i++) if (buttons[i] == code) return (int)i;
	return -1;
}

const InputAxis *InputDevice::Axis (int code) const
{
	for (auto &a : axes) if (a.code == code) return &a;
	return nullptr;
}

// ====================================================================================
// InputDevices

#define TEST_BIT(bit, array) ((array[(bit)/8] >> ((bit)%8)) & 1)

static std::string ReadLine (const std::string &path)
{
	std::ifstream f (path);
	std::string s;
	if (f) std::getline (f, s);
	return s;
}

InputDevices::InputDevices (const std::string &_sysroot, const std::string &_devroot)
	: sysroot (_sysroot), devroot (_devroot), nextId (1)
{
	inotifyFd = inotify_init1 (IN_NONBLOCK | IN_CLOEXEC);
	if (inotifyFd >= 0 && inotify_add_watch (inotifyFd, devroot.c_str(), IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MOVED_TO) < 0) {
		close (inotifyFd);
		inotifyFd = -1;
	}
}

InputDevices::~InputDevices ()
{
	for (auto &d : devs) Close (*d);
	if (inotifyFd >= 0) close (inotifyFd);
}

InputDevice *InputDevices::Get (int id)
{
	for (auto &d : devs) if (d->id == id) return d.get();
	return nullptr;
}

bool InputDevices::ReadSysfs (const std::string &sysname, InputDevice &d) const
{
	std::string base = sysroot + "/" + sysname + "/device/";
	std::ifstream test (base + "capabilities/ev");
	if (!test) return false;
	d.sysname = sysname;
	d.path = devroot + "/" + sysname;
	d.name = ReadLine (base + "name");
	d.uniq = ReadLine (base + "uniq");
	d.phys = ReadLine (base + "phys");
	d.vendor  = (unsigned)strtoul (ReadLine (base + "id/vendor").c_str(), nullptr, 16);
	d.product = (unsigned)strtoul (ReadLine (base + "id/product").c_str(), nullptr, 16);
	d.version = (unsigned)strtoul (ReadLine (base + "id/version").c_str(), nullptr, 16);
	InputParseBits (ReadLine (base + "capabilities/ev"), d.caps.ev);
	InputParseBits (ReadLine (base + "capabilities/key"), d.caps.key);
	InputParseBits (ReadLine (base + "capabilities/abs"), d.caps.abs);
	InputParseBits (ReadLine (base + "capabilities/rel"), d.caps.rel);
	InputParseBits (ReadLine (base + "properties"), d.caps.prop);
	return true;
}

void InputDevices::Scan (std::vector<InputDevEvent> *out)
{
	std::vector<std::string> nodes;
	if (DIR *dir = opendir (sysroot.c_str())) {
		while (struct dirent *e = readdir (dir))
			if (!strncmp (e->d_name, "event", 5)) nodes.push_back (e->d_name);
		closedir (dir);
	}
	std::sort (nodes.begin(), nodes.end(), [](const std::string &a, const std::string &b) {
		return atoi (a.c_str()+5) < atoi (b.c_str()+5); });
	for (auto &n : nodes) ScanNode (n, out);
}

void InputDevices::ScanNode (const std::string &sysname, std::vector<InputDevEvent> *out)
{
	for (auto &d : devs)
		if (d->sysname == sysname && d->state != InputDevice::GONE) {
			if (d->state == InputDevice::NOACCESS && Open (*d) && out)
				out->push_back ({InputDevEvent::ADDED, d->id, 0, 0, 0});
			return;
		}
	auto d = std::make_unique<InputDevice>();
	d->vendor = d->product = d->version = 0;
	d->fd = -1;
	d->err = 0;
	d->dropped = false;
	if (!ReadSysfs (sysname, *d) || !InputIsController (d->caps)) return;
	if (!Open (*d) && d->err == ENOENT) return; // the node is already gone again
	d->id = nextId++;
	if (out) out->push_back ({InputDevEvent::ADDED, d->id, 0, 0, 0});
	devs.push_back (std::move (d));
}

bool InputDevices::Open (InputDevice &d)
{
	for (int h = 0; h < 4; h++) {
		d.hatPresent[h] = d.caps.abs[ABS_HAT0X+2*h] || d.caps.abs[ABS_HAT0Y+2*h];
		d.hat[h][0] = d.hat[h][1] = 0;
		d.hatCenter[h][0] = d.hatCenter[h][1] = 0;
	}
	int fd = open (d.path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		d.err = errno;
		d.state = InputDevice::NOACCESS;
		return false;
	}
	d.fd = fd;
	d.err = 0;
	d.state = InputDevice::OPEN;
	d.dropped = false;

	unsigned char bits[KEY_MAX/8+1];
	memset (bits, 0, sizeof bits);
	if (ioctl (fd, EVIOCGBIT(0, sizeof bits), bits) >= 0) { // a real node: its own bits over sysfs
		for (int i = 0; i < EV_CNT; i++) d.caps.ev[i] = TEST_BIT(i, bits);
		memset (bits, 0, sizeof bits);
		if (ioctl (fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) >= 0) for (int i = 0; i < KEY_CNT; i++) d.caps.key[i] = TEST_BIT(i, bits);
		memset (bits, 0, sizeof bits);
		if (ioctl (fd, EVIOCGBIT(EV_ABS, sizeof bits), bits) >= 0) for (int i = 0; i < ABS_CNT; i++) d.caps.abs[i] = TEST_BIT(i, bits);
		memset (bits, 0, sizeof bits);
		if (ioctl (fd, EVIOCGBIT(EV_REL, sizeof bits), bits) >= 0) for (int i = 0; i < REL_CNT; i++) d.caps.rel[i] = TEST_BIT(i, bits);
		char name[256] = "";
		if (ioctl (fd, EVIOCGNAME(sizeof name), name) >= 0 && name[0]) d.name = name;
	}

	d.buttons.clear();
	for (int c = BTN_MISC; c <= KEY_MAX; c++) if (d.caps.key[c]) d.buttons.push_back (c);
	for (int c = 1; c < BTN_MISC; c++) if (d.caps.key[c]) d.buttons.push_back (c); // keyboard codes on a controller: after its buttons
	d.btn.assign (d.buttons.size(), 0);
	memset (bits, 0, sizeof bits);
	if (ioctl (fd, EVIOCGKEY(sizeof bits), bits) >= 0)
		for (size_t i = 0; i < d.buttons.size(); i++) d.btn[i] = TEST_BIT(d.buttons[i], bits);

	d.axes.clear();
	for (int a = 0; a < ABS_CNT; a++) {
		if (!d.caps.abs[a]) continue;
		struct input_absinfo ai;
		bool ok = (ioctl (fd, EVIOCGABS(a), &ai) >= 0);
		if (a >= ABS_HAT0X && a <= ABS_HAT3Y) {
			int h = (a-ABS_HAT0X)/2, xy = (a-ABS_HAT0X)%2;
			if (ok) {
				int c = (ai.minimum + ai.maximum)/2;
				d.hatCenter[h][xy] = c;
				d.hat[h][xy] = (ai.value < c ? -1 : ai.value > c ? 1 : 0);
			}
			continue;
		}
		InputAxis ax;
		ax.code = a;
		ax.min = (ok ? ai.minimum : -32767);
		ax.max = (ok ? ai.maximum : 32767);
		ax.value = (ok ? ai.value : (ax.min+ax.max)/2);
		d.axes.push_back (ax);
	}
	d.rels.clear();
	for (int r = 0; r < REL_CNT; r++) if (d.caps.rel[r]) d.rels.push_back (r);
	return true;
}

void InputDevices::Close (InputDevice &d)
{
	if (d.fd >= 0) close (d.fd);
	d.fd = -1;
}

void InputDevices::Remove (InputDevice &d, std::vector<InputDevEvent> &out)
{
	Close (d);
	d.state = InputDevice::GONE;
	out.push_back ({InputDevEvent::REMOVED, d.id, 0, 0, 0});
}

void InputDevices::Feed (InputDevice &d, const void *events, size_t n, std::vector<InputDevEvent> &out)
{
	const input_event *ev = (const input_event*)events;
	for (size_t i = 0; i < n; i++) {
		const input_event &e = ev[i];
		if (e.type == EV_SYN) {
			if (e.code == SYN_DROPPED) d.dropped = true;
			else if (e.code == SYN_REPORT && d.dropped) {
				d.dropped = false;
				Resync (d, out);
			}
			continue;
		}
		if (d.dropped) continue; // the kernel's queue overran: the state is read at the next report
		if (e.type == EV_KEY) {
			if (e.value == 2) continue; // autorepeat: still down
			int b = d.ButtonIndex (e.code);
			unsigned char v = (e.value ? 1 : 0);
			if (b >= 0 && d.btn[b] != v) {
				d.btn[b] = v;
				out.push_back ({InputDevEvent::BUTTON, d.id, b, v, 0});
			}
		} else if (e.type == EV_ABS) {
			if (e.code >= ABS_HAT0X && e.code <= ABS_HAT3Y) {
				int h = (e.code-ABS_HAT0X)/2, xy = (e.code-ABS_HAT0X)%2;
				int c = d.hatCenter[h][xy];
				int v = (e.value < c ? -1 : e.value > c ? 1 : 0);
				if (d.hat[h][xy] != v) {
					d.hat[h][xy] = v;
					out.push_back ({InputDevEvent::HAT, d.id, h, d.hat[h][0], d.hat[h][1]});
				}
			} else {
				for (auto &a : d.axes)
					if (a.code == e.code) {
						if (a.value != e.value) {
							a.value = e.value;
							out.push_back ({InputDevEvent::AXIS, d.id, a.code, a.value, 0});
						}
						break;
					}
			}
		} else if (e.type == EV_REL) {
			if (e.value && e.code < REL_CNT && d.caps.rel[e.code])
				out.push_back ({InputDevEvent::REL, d.id, e.code, e.value, 0});
		}
	}
}

void InputDevices::Resync (InputDevice &d, std::vector<InputDevEvent> &out)
{
	unsigned char bits[KEY_MAX/8+1];
	memset (bits, 0, sizeof bits);
	if (ioctl (d.fd, EVIOCGKEY(sizeof bits), bits) >= 0)
		for (size_t b = 0; b < d.buttons.size(); b++) {
			unsigned char v = TEST_BIT(d.buttons[b], bits);
			if (d.btn[b] != v) {
				d.btn[b] = v;
				out.push_back ({InputDevEvent::BUTTON, d.id, (int)b, v, 0});
			}
		}
	for (auto &a : d.axes) {
		struct input_absinfo ai;
		if (ioctl (d.fd, EVIOCGABS(a.code), &ai) >= 0 && ai.value != a.value) {
			a.value = ai.value;
			out.push_back ({InputDevEvent::AXIS, d.id, a.code, a.value, 0});
		}
	}
	for (int h = 0; h < 4; h++) {
		if (!d.hatPresent[h]) continue;
		int v[2] = {d.hat[h][0], d.hat[h][1]};
		for (int xy = 0; xy < 2; xy++) {
			struct input_absinfo ai;
			if (ioctl (d.fd, EVIOCGABS(ABS_HAT0X+2*h+xy), &ai) >= 0) {
				int c = d.hatCenter[h][xy];
				v[xy] = (ai.value < c ? -1 : ai.value > c ? 1 : 0);
			}
		}
		if (v[0] != d.hat[h][0] || v[1] != d.hat[h][1]) {
			d.hat[h][0] = v[0], d.hat[h][1] = v[1];
			out.push_back ({InputDevEvent::HAT, d.id, h, v[0], v[1]});
		}
	}
}

void InputDevices::Read (InputDevice &d, std::vector<InputDevEvent> &out)
{
	input_event ev[64];
	for (;;) {
		ssize_t n = read (d.fd, ev, sizeof ev);
		if (n > 0) {
			Feed (d, ev, (size_t)n/sizeof(ev[0]), out);
			continue;
		}
		if (n < 0 && errno == EINTR) continue;
		if (n < 0 && errno != EAGAIN) Remove (d, out); // ENODEV: unplugged
		break;
	}
}

void InputDevices::HotPlug (std::vector<InputDevEvent> &out)
{
	if (inotifyFd < 0) return;
	alignas(struct inotify_event) char buf[4096];
	for (;;) {
		ssize_t n = read (inotifyFd, buf, sizeof buf);
		if (n <= 0) break;
		for (char *p = buf; p < buf + n; ) {
			struct inotify_event *e = (struct inotify_event*)p;
			p += sizeof(struct inotify_event) + e->len;
			if (!e->len || strncmp (e->name, "event", 5)) continue;
			std::string name (e->name);
			if (e->mask & IN_DELETE) {
				for (auto &d : devs)
					if (d->sysname == name && d->state != InputDevice::GONE) Remove (*d, out);
			} else {
				ScanNode (name, &out);
			}
		}
	}
}

void InputDevices::Poll (std::vector<InputDevEvent> &out)
{
	HotPlug (out);
	for (auto &d : devs)
		if (d->state == InputDevice::OPEN) Read (*d, out);
	devs.erase (std::remove_if (devs.begin(), devs.end(),
		[](const std::unique_ptr<InputDevice> &d) { return d->state == InputDevice::GONE; }), devs.end());
}

std::vector<const InputDevice*> InputDevices::LegacyJoysticks () const
{
	std::vector<const InputDevice*> v;
	for (auto &d : devs)
		if (d->state == InputDevice::OPEN && InputIsLegacyJoystick (d->caps)) v.push_back (d.get());
	std::sort (v.begin(), v.end(), [](const InputDevice *a, const InputDevice *b) {
		return a->EventNumber() < b->EventNumber(); });
	return v;
}
