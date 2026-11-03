#!/usr/bin/env python3
# WineBridge generator: the bridged Orbiter functions, from the Windows SDK's declarations and the Linux SDK's.
#   wbgen.py --imports api/imports.txt --manual api/manual.txt --later api/later.txt --strings api/strings.txt --win-sdk DIR --linux-sdk DIR
#            --wine-include DIR --clang-res DIR --out DIR
import argparse, hashlib, os, re, sys
import clang.cindex as ci

HANDLES = {'OBJHANDLE', 'VISHANDLE', 'MESHHANDLE', 'DEVMESHHANDLE', 'SURFHANDLE', 'FILEHANDLE', 'NOTEHANDLE', 'NAVHANDLE',
	'PSTREAM_HANDLE', 'CTRLSURFHANDLE', 'AIRFOILHANDLE', 'THRUSTER_HANDLE', 'THGROUP_HANDLE', 'PROPELLANT_HANDLE', 'DOCKHANDLE',
	'ATTACHMENTHANDLE', 'ANIMATIONCOMPONENT_HANDLE', 'PANELHANDLE', 'LAUNCHPADITEM_HANDLE'}
RAW = {'VECTOR3', 'MATRIX3', 'COLOUR4'}
FUNCS = (ci.CursorKind.FUNCTION_DECL, ci.CursorKind.CXX_METHOD, ci.CursorKind.CONSTRUCTOR, ci.CursorKind.DESTRUCTOR)
INTS = {ci.TypeKind.CHAR_S, ci.TypeKind.SCHAR, ci.TypeKind.UCHAR, ci.TypeKind.SHORT, ci.TypeKind.USHORT, ci.TypeKind.INT,
	ci.TypeKind.UINT, ci.TypeKind.LONG, ci.TypeKind.ULONG, ci.TypeKind.CHAR_U, ci.TypeKind.ENUM}
REFS = (ci.TypeKind.LVALUEREFERENCE, ci.TypeKind.POINTER)

def die (msg):
	sys.exit ('wbgen: ' + msg)

def parse (path, args, text):
	tu = ci.Index.create ().parse (path, args = args, unsaved_files = [(path, text)])
	errs = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
	if errs:
		die ('%s: %s' % (path, '; '.join (str (d) for d in errs[:5])))
	return tu

def bare (t):  # the type's name as spelled, without const, & and *
	s = re.sub (r'\bconst\b', '', t.spelling)
	return re.sub (r'[&*\s]+$', '', s).strip ()

def scalar (t):
	k = t.get_canonical ().kind
	if k == ci.TypeKind.BOOL:
		return 'b'
	if k in INTS:
		return 'i'
	if k in (ci.TypeKind.FLOAT, ci.TypeKind.DOUBLE):
		return 'f'
	return None

def layout (t):  # a struct's layout as text if it holds no pointers, else None
	t = t.get_canonical ()
	if t.kind != ci.TypeKind.RECORD:
		return None
	out = []
	for f in t.get_fields ():
		ft, n = f.type.get_canonical (), 1
		while ft.kind == ci.TypeKind.CONSTANTARRAY:
			n *= ft.get_array_size ()
			ft = ft.get_array_element_type ().get_canonical ()
		if ft.kind == ci.TypeKind.RECORD and bare (f.type) in RAW:
			k = bare (f.type)
		elif ft.kind == ci.TypeKind.RECORD:
			sub = layout (ft)
			if sub is None:
				return None
			k = '{%s}' % sub
		elif scalar (ft):
			k = '%s%d' % (scalar (ft), ft.get_size ())
		else:
			return None
		out.append ('%s@%d:%sx%d' % (f.spelling, t.get_offset (f.spelling) // 8, k, n))
	return '%d|%s' % (t.get_size (), ','.join (out))

def form (t):  # a parameter's or result's wire form; None when it needs code by hand
	c = t.get_canonical ()
	name = bare (t)
	if name in HANDLES:  # a handle is a pointer typedef: only a reference to it or a pointer to it changes the form
		if t.kind == ci.TypeKind.LVALUEREFERENCE:
			return 'h' if t.get_pointee ().is_const_qualified () else 'hio'
		return None if t.kind == ci.TypeKind.POINTER else 'h'
	if c.kind == ci.TypeKind.VOID:
		return 'void'
	if scalar (c):
		return scalar (c)
	if c.kind == ci.TypeKind.RECORD:
		return 'rec:' + name
	if c.kind in REFS:
		p = t.get_pointee ()
		pc = p.get_canonical ()
		opt = '' if c.kind == ci.TypeKind.LVALUEREFERENCE else '?'
		if pc.kind in (ci.TypeKind.CHAR_S, ci.TypeKind.CHAR_U) and c.kind == ci.TypeKind.POINTER:
			return 'cstr' if p.is_const_qualified () else 'chars'  # chars: api/strings.txt says in or out
		if pc.kind == ci.TypeKind.RECORD:
			return ('rec:' if p.is_const_qualified () else 'out:rec:') + bare (t) + opt
		if p.is_const_qualified ():
			return None
		if scalar (pc):
			return 'out:' + scalar (pc) + opt
	return None

class Side:
	def __init__ (self, tu):
		self.by_mangled, self.by_name, self.records = {}, {}, {}
		for c in tu.cursor.walk_preorder ():
			if c.kind in FUNCS:
				self.by_mangled.setdefault (c.mangled_name, c)
				self.by_name.setdefault (qual (c), []).append (c)
			elif c.kind in (ci.CursorKind.STRUCT_DECL, ci.CursorKind.CLASS_DECL) and c.is_definition () and c.spelling:
				self.records.setdefault (c.spelling, c.type)
			elif c.kind == ci.CursorKind.TYPEDEF_DECL and c.underlying_typedef_type.get_canonical ().kind == ci.TypeKind.RECORD:
				self.records.setdefault (c.spelling, c.underlying_typedef_type)

def qual (c):
	parts, p = [c.spelling], c.semantic_parent
	while p is not None and p.kind != ci.CursorKind.TRANSLATION_UNIT:
		if p.spelling:
			parts.append (p.spelling)
		p = p.semantic_parent
	return '::'.join (reversed (parts))

def enum_of (t):
	c = t.get_canonical ()
	if c.kind in REFS:
		c = c.get_pointee ().get_canonical ()
	return c if c.kind == ci.TypeKind.ENUM else None

def enum_values (t):
	return {x.spelling: x.enum_value for x in t.get_declaration ().get_children () if x.kind == ci.CursorKind.ENUM_CONSTANT_DECL}

def rec_type (t):
	c = t.get_canonical ()
	return c.get_pointee () if c.kind in REFS else c

def main ():
	ap = argparse.ArgumentParser ()
	for a in ('imports', 'manual', 'later', 'strings', 'win-sdk', 'linux-sdk', 'wine-include', 'clang-res', 'out'):
		ap.add_argument ('--' + a, required = True)
	ap.add_argument ('--linux-syms') # what Orbiter and the Orbitersdk library define: a Linux function only declared isn't called
	a = ap.parse_args ()
	rows = lambda f: [l.split ('#')[0].split () for l in open (f) if l.split ('#')[0].strip ()]
	imports = sorted ({r[0] for r in rows (a.imports)})
	manual = {r[0] for r in rows (a.manual)}
	later = {r[0] for r in rows (a.later)}  # not bridged yet: neither generated nor exported (api/later.txt)
	syms = {l.strip () for l in open (a.linux_syms)} if a.linux_syms else None
	strings = {}  # (mangled, parameter): 'in' or the size of an out buffer ('pN': parameter N, or a number)
	for r in rows (a.strings):
		strings[(r[0], int (r[1]))] = r[2] if r[2] in ('in', 'text') else r[3]
	shim = os.path.join (os.path.dirname (os.path.abspath (__file__)), '..', 'helper', 'shim')
	win = Side (parse ('win.cpp', ['-x', 'c++', '--target=i686-windows', '-fms-extensions', '-fms-compatibility', '-nostdinc',
		'-isystem', a.clang_res + '/include', '-isystem', a.wine_include + '/msvcrt', '-isystem', a.wine_include + '/windows',
		'-isystem', shim, '-isystem', a.win_sdk, '-DSTRICT', '-DOAPI_IMPLEMENTATION'],
		'#include "Orbitersdk.h"\n#include "MFDAPI.h"\n#include "CelBodyAPI.h"\n#include "DrawAPI.h"\n'))
	lin = Side (parse ('lin.cpp', ['-x', 'c++', '-std=c++20', '--target=x86_64-linux-gnu', '-resource-dir', a.clang_res, '-I', a.linux_sdk],
		'#include "Orbitersdk.h"\n'))

	gen, need, pods, absent = [], [], set (), []
	for m in imports:
		if m in manual or m in later:
			continue
		c = win.by_mangled.get (m)
		if c is None:
			need.append ('%s: not in the Windows SDK headers' % m)
			continue
		if c.is_definition ():
			need.append ('%s: inline in the Windows SDK header' % m)
			continue
		if c.type.kind == ci.TypeKind.FUNCTIONPROTO and c.type.is_function_variadic ():
			need.append ('%s: variadic' % m)
			continue
		member = c.kind == ci.CursorKind.CXX_METHOD
		if not (c.kind == ci.CursorKind.FUNCTION_DECL or (member and c.semantic_parent.spelling == 'VESSEL' and not c.is_static_method ())):
			need.append ('%s: %s' % (m, qual (c)))
			continue
		args = list (c.get_arguments ())
		forms = [form (x.type) for x in args]
		for i, f in enumerate (forms):
			s = strings.get ((m, i))
			if f in ('chars', 'cstr') and s == 'text':  # a module's text: UTF-8 for Orbiter (helper/text.cpp)
				forms[i] = 'text'
			elif f == 'chars':
				forms[i] = None if s is None else 'cstr' if s == 'in' else 'buf:' + s
		res = form (c.result_type)
		sig = '%s %s (%s)' % (c.result_type.spelling, qual (c), ', '.join (x.type.spelling for x in args))
		if res is None or res.startswith (('out:', 'cstr', 'chars')) or res.endswith ('?') or None in forms:
			need.append ('%s: %s%s' % (m, sig, ' (a char * parameter: api/strings.txt)' if 'chars' in [form (x.type) for x in args] and None in forms else ''))
			continue
		lname = qual (c)
		pick = None
		for lc in lin.by_name.get (lname, []) + (lin.by_name.get (lname[:-1], []) if lname.endswith ('A') else []):  # a Windows macro may append A
			la = list (lc.get_arguments ())
			lf = [form (x.type) for x in la]
			if len (lf) != len (forms) or form (lc.result_type) != res:
				continue
			ok = True
			for f, g, wa, lx in zip (forms, lf, args, la):
				if f.startswith ('buf:') or f in ('cstr', 'text'):
					ok = ok and g in ('cstr', 'chars')
				else:
					ok = ok and f == g
					if f in ('h', 'hio'):  # a handle's kind too: oapiSetTexture takes a MESHHANDLE or a DEVMESHHANDLE
						ok = ok and bare (wa.type) == bare (lx.type)
			if ok:
				pick = lc
				break
		if pick is None:
			need.append ('%s: no Linux %s with the same parameters' % (m, sig))
			continue
		if syms is not None and pick.mangled_name not in syms:
			absent.append (m) # this Orbiter doesn't define it: not exported (gen/absent.txt), as later.txt
			continue
		wtypes = [x.type for x in args] + [c.result_type]
		ltypes = [x.type for x in pick.get_arguments ()] + [pick.result_type]
		why = None
		for f, wt, lt in zip (forms + [res], wtypes, ltypes):
			if 'rec:' in f and f.split ('rec:')[1].rstrip ('?') not in RAW:
				wl, ll = layout (rec_type (wt)), layout (rec_type (lt))
				if wl is None or wl != ll:
					why = 'struct %s: %s on Windows, %s on Linux' % (f.split ('rec:')[1].rstrip ('?'), wl, ll)
					break
				pods.add ((bare (lt), rec_type (wt).get_canonical ().get_size ()))
			we, le = enum_of (wt), enum_of (lt)
			if we is not None:
				wv, lv = enum_values (we), enum_values (le) if le is not None else {}
				diff = [k for k, v in wv.items () if lv.get (k) != v]
				if diff:
					why = 'enum %s differs on Linux: %s' % (we.spelling, ', '.join (diff[:6]))
					break
		if why:
			need.append ('%s: %s' % (m, why))
			continue
		gen.append ((m, c, pick, forms, res))

	if need:
		die ('%d imports need code by hand (then listed in api/manual.txt):\n  %s' % (len (need), '\n  '.join (need)))
	h = hashlib.sha1 (('\n'.join (imports) + '\n'.join (sorted (later)) + open (__file__).read ()).encode ()).hexdigest ()[:8]
	os.makedirs (a.out, exist_ok = True)
	write (os.path.join (a.out, 'gen_calls.h'), calls_h (gen, h))
	write (os.path.join (a.out, 'absent.txt'), ''.join ('%s # declared in the Linux SDK, defined by no part of this Orbiter\n' % m for m in absent))
	write (os.path.join (a.out, 'gen_helper.cpp'), helper_cpp (gen))
	write (os.path.join (a.out, 'gen_serve.cpp'), serve_cpp (gen, sorted (pods)))
	print ('wbgen: %d generated, %d written by hand, %d not defined by this Orbiter, list %s' % (len (gen), len (manual & set (imports)), len (absent), h))

def write (path, text):
	if not os.path.exists (path) or open (path).read () != text:
		open (path, 'w').write (text)

def ident (i, c):
	return 'LG_%s_%d' % (qual (c).replace ('::', '_'), i)

def calls_h (gen, h):
	o = ['// generated by tools/wbgen.py', '#pragma once', '#define WB_GEN_HASH 0x%su' % h, 'enum WbGenCall {']
	o += ['\t%s = %d,' % (ident (i, c), 1000 + i) for i, (m, c, *_r) in enumerate (gen)]
	o += ['\tLG_END = %d' % (1000 + len (gen)), '};']
	return '\n'.join (o) + '\n'

def size_expr (s):  # an out buffer's size: parameter N or a number
	return 'a' + s[1:] if s.startswith ('p') else s

def helper_cpp (gen):
	o = ['// generated by tools/wbgen.py: the Orbiter functions the helper forwards to Linux', '#define OAPI_IMPLEMENTATION',
		'#include "Orbitersdk.h"', '#include "wb_helper.h"', '#include "gen_calls.h"', '']
	for i, (m, c, pick, forms, res) in enumerate (gen):
		args = list (c.get_arguments ())
		const = ' const' if c.kind == ci.CursorKind.CXX_METHOD and c.is_const_method () else ''
		o.append ('%s %s (%s)%s' % (c.result_type.spelling, qual (c), ', '.join ('%s a%d' % (x.type.spelling, k) for k, x in enumerate (args)), const))
		o.append ('{')
		for k, x in enumerate (args):  # the module's changed copies of that mesh go to Orbiter first (manual.cpp)
			if bare (x.type) == 'MESHHANDLE':
				o.append ('\twb::MeshUse (a%d);' % k)
		if qual (c) in ('VESSEL::AddMesh', 'VESSEL::InsertMesh') and 'cstr' in forms:
			o.append ('\twb::MeshUse (0); // a mesh loaded by name: any copy may be of it')
		writes = c.kind == ci.CursorKind.CXX_METHOD or len (forms) > 0
		o.append (('\tWbWriter &w = wb::Begin (%s);' if writes else '\twb::Begin (%s);') % ident (i, c))
		if c.kind == ci.CursorKind.CXX_METHOD:
			o.append ('\tw.u32 (wb::Id (vessel));')
		for k, f in enumerate (forms):
			a = 'a%d' % k
			if f in ('h', 'hio'):
				o.append ('\tw.u32 (wb::Id (%s));' % a)
			elif f == 'b':
				o.append ('\tw.b (%s);' % a)
			elif f == 'i':
				o.append ('\tw.i32 ((int32_t)%s);' % a)
			elif f == 'f':
				o.append ('\tw.f64 (%s);' % a)
			elif f == 'cstr':
				o.append ('\tw.str (%s);' % a)
			elif f == 'text':
				o.append ('\twb::TextStr (w, %s);' % a)
			elif f.startswith ('buf:'):
				o.append ('\tw.i32 ((int32_t)(%s)); wb::InStr (w, %s, %s);' % (size_expr (f[4:]), a, size_expr (f[4:])))
			elif f.startswith ('rec:'):
				o.append (('\tw.b (%s != 0); if (%s) w.raw (%s, sizeof *%s);' % (a, a, a, a)) if f.endswith ('?') else ('\tw.raw (&%s, sizeof %s);' % (a, a)))
			elif f.startswith ('out:'):  # in and out: Orbiter may leave it as it is
				opt, kind = f.endswith ('?'), f[4:].rstrip ('?')
				v = '*' + a if opt else a
				put = ('w.raw (&%s, sizeof %s)' % (v, v)) if kind.startswith ('rec:') else {'b': 'w.b (%s)', 'i': 'w.i32 ((int32_t)%s)', 'f': 'w.f64 (%s)'}[kind] % v
				o.append (('\tw.b (%s != 0); if (%s) %s;' % (a, a, put)) if opt else ('\t%s;' % put))
		reads = res != 'void' or any (f == 'hio' or f.startswith (('buf:', 'out:')) for f in forms)
		o.append ('\tWbReader r = wb::Call ();' if reads else '\twb::Call ();')
		for k, f in enumerate (forms):
			a = 'a%d' % k
			if f == 'hio':
				o.append ('\t%s = (%s)wb::Ptr (r.u32 ());' % (a, bare (args[k].type)))
			elif f.startswith ('buf:'):
				o.append ('\twb::OutStrIf (r, %s, %s);' % (a, size_expr (f[4:])))
			elif f.startswith ('out:'):
				opt, kind, t = f.endswith ('?'), f[4:].rstrip ('?'), bare (args[k].type)
				if kind.startswith ('rec:'):
					o.append (('\tif (%s) r.raw (%s, sizeof *%s);' % (a, a, a)) if opt else ('\tr.raw (&%s, sizeof %s);' % (a, a)))
				else:
					expr = {'b': 'r.b ()', 'i': '(%s)r.i32 ()' % t, 'f': '(%s)r.f64 ()' % t}[kind]
					o.append (('\tif (%s) *%s = %s;' % (a, a, expr)) if opt else ('\t%s = %s;' % (a, expr)))
		rt = c.result_type.spelling
		if res == 'h':
			o.append ('\treturn (%s)wb::Ptr (r.u32 ());' % rt)
		elif res == 'b':
			o.append ('\treturn r.b ();')
		elif res == 'i':
			o.append ('\treturn (%s)r.i32 ();' % rt)
		elif res == 'f':
			o.append ('\treturn (%s)r.f64 ();' % rt)
		elif res.startswith ('rec:'):
			o.append ('\t%s v; r.raw (&v, sizeof v); return v;' % bare (c.result_type))
		o += ['}', '']
	return '\n'.join (o)

def hkind (t):  # not upstream (design-security-fixes F2): a handle parameter's kind on the Linux side
	return 'WK_' + t if t in HANDLES else 'WK_ANY'

RETIRE = {'oapiDestroySurface'}  # not upstream (F2): its handle names nothing after the call

def serve_cpp (gen, pods):
	o = ['// generated by tools/wbgen.py: the Linux side of the generated calls', '#include "Orbitersdk.h"', '#include "Bridge.h"',
		'#include "gen_calls.h"', '#include <algorithm>', '#include <cstring>', '#include <string>', '#include <vector>', '']
	o += ['static_assert (sizeof (%s) == %d, "%s: the Windows layout");' % (p, n, p) for p, n in pods]
	o += ['', 'const char *WbGenName (uint32_t fn)', '{', '\tstatic const char *const names[] = {']
	o += ['\t\t"%s",' % qual (c) for (m, c, *_r) in gen]
	o += ['\t};', '\treturn fn >= 1000 && fn < LG_END ? names[fn - 1000] : nullptr;', '}', '']
	o += ['bool WbServeGen (uint32_t fn, WbReader &r, WbWriter &w)', '{', '\tWbBridge &b = WbBridge::Get ();', '\tswitch (fn) {']
	for i, (m, c, pick, forms, res) in enumerate (gen):
		largs = list (pick.get_arguments ())
		member = c.kind == ci.CursorKind.CXX_METHOD
		o.append ('\tcase %s: {' % ident (i, c))
		if member:
			o.append ('\t\tuint32_t vid = r.u32 ();')
		call, post = [], []
		for k, f in enumerate (forms):
			lt = largs[k].type
			t, a = bare (lt), 'a%d' % k
			if f in ('h', 'hio'):
				o.append ('\t\t%s %s = (%s)b.Ptr (r.u32 (), %s);' % (t, a, t, hkind (t)))
				call.append (a)
				if f == 'hio':
					post.append ('\t\tw.u32 (b.Id (%s, %s));' % (a, hkind (t)))
				if qual (c) in RETIRE and k == 0:
					post.append ('\t\tb.Retire (%s);' % a)
			elif f == 'b':
				o.append ('\t\tbool %s = r.b ();' % a)
				call.append (a)
			elif f == 'i':
				o.append ('\t\t%s %s = (%s)r.i32 ();' % (t, a, t))
				call.append (a)
			elif f == 'f':
				o.append ('\t\t%s %s = (%s)r.f64 ();' % (t, a, t))
				call.append (a)
			elif f in ('cstr', 'text'):
				o.append ('\t\tuint32_t n%d; const char *s%d = r.str (&n%d); std::string %s (s%d ? s%d : "", s%d ? n%d : 0);' % ((k,) * 2 + (k, a) + (k,) * 4))
				call.append ('s%d ? %s.data () : nullptr' % (k, a))
			elif f.startswith ('buf:'):
				o.append ('\t\tint32_t n%d = r.i32 (); std::vector<char> %s (n%d > 0 && n%d <= (1 << 20) ? n%d : 1, 0);' % ((k, a) + (k,) * 3))
				o.append ('\t\t{ uint32_t l; const char *s = r.str (&l); if (s) memcpy (%s.data (), s, std::min<size_t> (l, %s.size () - 1)); }' % (a, a))
				o.append ('\t\tconst std::vector<char> %s0 = %s; // as the module had it: written back only if Orbiter changes it' % (a, a))
				call.append ('%s.data ()' % a)
				post.append ('\t\t%s.back () = 0; w.b (%s != %s0); if (%s != %s0) w.str (%s.data ());' % ((a,) * 6))
			elif f.startswith ('rec:'):
				if f.endswith ('?'):
					o.append ('\t\tbool p%d = r.b (); %s %s {}; if (p%d) r.raw (&%s, sizeof %s);' % (k, t, a, k, a, a))
					call.append ('p%d ? &%s : nullptr' % (k, a))
				else:
					o.append ('\t\t%s %s; r.raw (&%s, sizeof %s);' % (t, a, a, a))
					call.append (a)
			elif f.startswith ('out:'):
				opt, kind = f.endswith ('?'), f[4:].rstrip ('?')
				o.append ('\t\t%s %s {};' % (t, a))
				get = ('r.raw (&%s, sizeof %s)' % (a, a)) if kind.startswith ('rec:') else {'b': '%s = r.b ()' % a, 'i': '%s = (%s)r.i32 ()' % (a, t), 'f': '%s = (%s)r.f64 ()' % (a, t)}[kind]
				o.append (('\t\tbool p%d = r.b (); if (p%d) %s;' % (k, k, get)) if opt else ('\t\t%s; // the module\'s value: Orbiter may leave it' % get))
				call.append (('p%d ? &%s : nullptr' % (k, a)) if opt else a)
				body = 'w.raw (&%s, sizeof %s)' % (a, a) if kind.startswith ('rec:') else {'b': 'w.b (%s)', 'i': 'w.i32 ((int32_t)%s)', 'f': 'w.f64 ((double)%s)'}[kind] % a
				post.append (('\t\tif (p%d) %s;' % (k, body)) if opt else ('\t\t%s;' % body))
		o.append ('\t\tif (r.bad) return true;')
		if member:
			o.append ('\t\tVESSEL *v = b.Vessel (vid);')
			o.append ('\t\tif (!v) { b.NoVessel (fn, vid); return true; }')
			callx = 'v->%s (%s)' % (pick.spelling, ', '.join (call))
		else:
			friend = pick.lexical_parent is not None and pick.lexical_parent.kind in (ci.CursorKind.CLASS_DECL, ci.CursorKind.STRUCT_DECL)
			callx = '%s%s (%s)' % ('' if friend else '::', pick.spelling, ', '.join (call)) # a friend declared only in its class: found through its argument
		if res == 'void':
			o.append ('\t\t%s;' % callx)
		else:
			o.append ('\t\tauto res = %s;' % callx)
		o += post
		o.append ({'void': '', 'h': '\t\tw.u32 (b.Id (res, %s));' % hkind (bare (pick.result_type)), 'b': '\t\tw.b (res);', 'i': '\t\tw.i32 ((int32_t)res);', 'f': '\t\tw.f64 ((double)res);'}.get (res, '\t\tw.raw (&res, sizeof res);'))
		o.append ('\t\treturn true; }')
	o += ['\tdefault:', '\t\treturn false;', '\t}', '}']
	return '\n'.join (l for l in o if l != '') + '\n'

main ()
