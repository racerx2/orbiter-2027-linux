
// custom: forms skins; a built form with its JavaScript: bindings, actions, lists, keys, ticks, toast

#include "FormRuntime.h"
#include "FormPainted.h"
#include "FormWidgets.h"
#include "Orbits.h"
#include <QAbstractButton>
#include <QApplication>
#include <QBoxLayout>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QGroupBox>
#include <QJSValueIterator>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMetaEnum>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabWidget>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace forms {

namespace {

	qint64 NowMs ()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now ().time_since_epoch ()).count ();
	}

	bool IsTemplate (const UiWidget &c)
	{
		const UiValue *t = c.Dyn ("template");
		return t && ((t->type == UiValue::BOOL && t->flag) || (t->type == UiValue::STRING && t->str.trimmed () == "true"));
	}

	bool RuntimeName (const QString &n)
	{
		return n == "bind" || n.startsWith ("bind_") || n == "showIf" || n == "enableIf" || n == "model" || n == "action"
			|| n == "doubleAction" || n.startsWith ("key_") || n == "list" || n == "templateIf" || n == "scripts";
	}

	QString Src (const UiValue &v)
	{
		return (v.type == UiValue::STRINGLIST ? v.list.join ('\n') : v.str);
	}

	const QStringList RESERVED = {"Launcher", "Orbits", "view", "ui", "w", "toast", "update"};

}

Node::~Node () = default;

// ---------------------------------------------------------------------------------------------------------
// ViewApi

ViewApi::ViewApi (FormRuntime *r): QObject (r), rt (r) {}
int ViewApi::width () const { return rt->View () ? rt->View ()->width () : 0; }
int ViewApi::height () const { return rt->View () ? rt->View ()->height () : 0; }
int ViewApi::revision () const { return rt->Revision (); }
void ViewApi::toast (const QString &text) { rt->Toast (text); }

bool ViewApi::hasFocus (const QString &name) const
{
	QWidget *w = rt->Find (name);
	QWidget *f = QApplication::focusWidget ();
	return w && f && (w == f || w->isAncestorOf (f));
}

void ViewApi::focus (const QString &name)
{
	if (QWidget *w = rt->Find (name)) w->setFocus (Qt::OtherFocusReason);
}

// ---------------------------------------------------------------------------------------------------------
// setup and teardown

FormRuntime::FormRuntime (QWidget *v, QObject *l, std::function<void (const QString &)> lg)
	: QObject (v), view (v), launcher (l), log (std::move (lg))
{
	rootCtx = std::make_shared<Ctx> ();
	rootCtx->item = QJSValue (QJSValue::UndefinedValue);
	toastTimer.setSingleShot (true);
	toastTimer.setInterval (2400);
	connect (&toastTimer, &QTimer::timeout, this, [this]() { if (toastLabel) Fade (toastLabel, false, 200); });
	watchdog = std::thread ([this]() {
		std::unique_lock<std::mutex> lk (wdMutex);
		while (!wdStop) {
			if (!busySince.load ()) wdCond.wait (lk, [this]() { return wdStop || busySince.load () != 0; }); // idle: no wake-ups
			else wdCond.wait_for (lk, std::chrono::milliseconds (200));
			const qint64 s = busySince.load ();
			QJSEngine *e = wdEngine.load ();
			if (s && e && NowMs () - s > 2000) e->setInterrupted (true); // thread-safe in Qt
		}
	});
}

void FormRuntime::BeginBusy ()
{
	std::lock_guard<std::mutex> lk (wdMutex); // the watchdog checks under it, so no old interrupt lands after the clear
	if (engine && engine->isInterrupted ()) engine->setInterrupted (false); // set while C++ built widgets, or late for the last call
	busySince = NowMs (); // each JavaScript entry inside a refresh gets its own 2 s
	if (depth++ == 0) wdCond.notify_one ();
}

void FormRuntime::EndBusy ()
{
	if (--depth > 0) return;
	busySince = 0;
}

FormRuntime::~FormRuntime ()
{
	Shutdown ();
}

void FormRuntime::Shutdown ()
{
	if (dying) return;
	dying = true;
	if (launcher) disconnect (launcher, nullptr, this, nullptr);
	disconnect (qApp, nullptr, this, nullptr);
	for (auto &[ms, t] : ticks) t->stop ();
	toastTimer.stop ();
	{
		std::lock_guard<std::mutex> lk (wdMutex);
		wdStop = true;
	}
	wdCond.notify_all ();
	if (watchdog.joinable ()) watchdog.join ();
	nodes.clear ();
	if (glow) glow->Clear ();
	QWidget *r = root;
	root = nullptr;
	delete r;           // every widget of the form
	rootNode.reset ();
	compiled.clear ();
	updateFn = wObj = QJSValue ();
	if (rootCtx) rootCtx->item = QJSValue ();
	wdEngine = nullptr;
	delete engine;      // last: the widgets' bindings are gone
	engine = nullptr;
}

void FormRuntime::Warn (const QString &s)
{
	warnings++;
	if (!log) return;
	if (warnings <= 100) log (s);
	else if (warnings == 101) log ("further warnings are left out");
}

QStringList FormRuntime::Files () const
{
	return scriptFiles;
}

bool FormRuntime::Load (const QString &formFile, const QString &skinDir, QString &err)
{
	const QFileInfo fi (formFile);
	const QString name = fi.fileName ();
	env.skinDir = QFileInfo (skinDir).canonicalFilePath ();
	env.formDir = QFileInfo (fi.canonicalFilePath ()).absolutePath ();
	env.warn = [this, name](const QString &s) { Warn (name + ": " + s); };
	if (env.skinDir.isEmpty () || fi.canonicalFilePath ().isEmpty () || !fi.canonicalFilePath ().startsWith (env.skinDir + '/')) {
		err = name + ": the form must be inside the skin folder";
		return false;
	}
	custom::UiLimits lim;
	lim.maxBytes = 4 << 20;
	lim.maxWidgets = 4000;
	if (!custom::ReadUiFile (fi.canonicalFilePath (), form, err, lim)) {
		err = name + ": " + err;
		return false;
	}
	if (!CheckClasses (form.root, err)) { // templates too: they are built later
		err = name + ": " + err;
		return false;
	}
	scriptFiles << fi.canonicalFilePath ();
	for (const QString &res : form.resources) {
		QString qerr;
		if (!ReadQrc (res, env, qerr)) Warn (name + ": " + qerr);
		else scriptFiles << ResolveFile (env, res);
	}

	engine = new QJSEngine;
	wdEngine = engine;
	QJSValue g = engine->globalObject ();
	if (launcher) {
		QJSEngine::setObjectOwnership (launcher, QJSEngine::CppOwnership);
		g.setProperty ("Launcher", engine->newQObject (launcher));
	}
	orbitsApi = new OrbitsApi (this);
	QJSEngine::setObjectOwnership (orbitsApi, QJSEngine::CppOwnership);
	g.setProperty ("Orbits", engine->newQObject (orbitsApi));
	viewApi = new ViewApi (this);
	QJSEngine::setObjectOwnership (viewApi, QJSEngine::CppOwnership);
	g.setProperty ("view", engine->newQObject (viewApi));
	g.setProperty ("ui", engine->newObject ());
	wObj = engine->newObject ();
	g.setProperty ("w", wObj);
	engine->evaluate ("function toast (t) { view.toast (t); }");
	if (!LoadScripts (err)) return false;
	updateFn = g.property ("update");
	if (!updateFn.isCallable ()) updateFn = QJSValue ();

	QString berr;
	rootNode = Build (form.root, view, nullptr, rootCtx, false, berr);
	if (!rootNode) {
		err = name + ": " + berr;
		return false;
	}
	root = rootNode->w;
	if (launcher) {
		const QMetaObject *mo = launcher->metaObject ();
		const int slot = metaObject ()->indexOfSlot ("LauncherSignal()");
		for (int i = QObject::staticMetaObject.methodCount (); i < mo->methodCount (); i++)
			if (mo->method (i).methodType () == QMetaMethod::Signal) QMetaObject::connect (launcher, i, this, slot);
		active = !launcher->property ("active").toBool (); // SetActive below runs its change
		SetActive (!active);
	} else SetActive (false);
	connect (qApp, &QApplication::focusChanged, this, [this](QWidget *a, QWidget *b) {
		if (root && ((a && root->isAncestorOf (a)) || (b && root->isAncestorOf (b)))) ScheduleRefresh ();
	});
	if (view) root->setGeometry (view->rect ());
	Refresh ();
	return true;
}

bool FormRuntime::LoadScripts (QString &err)
{
	const UiValue *s = form.root.Dyn ("scripts");
	if (!s) return true;
	QStringList files = (s->type == UiValue::STRINGLIST ? s->list : s->str.split (QRegularExpression ("[;,]"))); // names may have spaces
	for (QString &f : files) f = f.trimmed ();
	files.removeAll (QString ());
	if (files.size () > 16) {
		err = "more than 16 scripts";
		return false;
	}
	for (const QString &f : files) {
		const QString path = ResolveFile (env, f);
		if (path.isEmpty ()) { err = "script " + f + " is not a file inside the skin folder"; return false; }
		QFile file (path);
		if (!file.open (QIODevice::ReadOnly) || file.size () > (1 << 20)) { err = "script " + f + " can't be read or is larger than 1 MiB"; return false; }
		const QString text = QString::fromUtf8 (file.readAll ());
		QJSValue r;
		bool interrupted = false;
		{
			Busy busy (this);
			r = engine->evaluate (text, QFileInfo (path).fileName (), 1);
			interrupted = engine->isInterrupted ();
		}
		if (interrupted) {
			engine->setInterrupted (false);
			err = QFileInfo (path).fileName () + ": stopped after running 2 s";
			return false;
		}
		if (r.isError () || engine->hasError ()) {
			QJSValue e = (engine->hasError () ? engine->catchError () : r);
			err = QFileInfo (path).fileName () + " line " + e.property ("lineNumber").toString () + ": " + e.toString ();
			return false;
		}
		scriptFiles << path;
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// JavaScript

QJSValue FormRuntime::Compile (const QString &src, const QString &where, bool statement, bool setter)
{
	if (src.trimmed ().isEmpty ()) return QJSValue ();
	QString code;
	if (setter) code = "(function (value, item, index) {\n" + src + " = value;\n})";
	else if (statement) code = "(function (item, index) {\n" + src + "\n})";
	else code = "(function (item, index) { return (\n" + src + "\n); })";
	QJSValue f;
	{
		Busy busy (this); // the source can close the function and run code at once
		f = engine->evaluate (code, where, 0);
	}
	if (engine->isInterrupted ()) {
		engine->setInterrupted (false);
		Warn (where + ": stopped after running 2 s");
		return QJSValue ();
	}
	if (engine->hasError ()) f = engine->catchError ();
	if (f.isError () || !f.isCallable ()) {
		Warn (where + ": " + f.toString ());
		return QJSValue ();
	}
	return f;
}

QJSValue FormRuntime::CompileOnce (const UiValue &v, const QString &where, bool statement, bool setter)
{
	const auto key = std::make_pair (static_cast<const void*> (&v), (statement ? 1 : 0) | (setter ? 2 : 0));
	auto it = compiled.find (key);
	if (it != compiled.end ()) return it->second;
	QJSValue f = Compile (Src (v), where, statement, setter);
	compiled[key] = f; // a failed one too: it warns once
	return f;
}

bool FormRuntime::CheckClasses (const UiWidget &u, QString &err) const
{
	if (!KnownClass (u.cls)) {
		const custom::UiCustom *c = form.Custom (u.cls);
		if (!c || !KnownClass (c->extends)) {
			err = QString ("class %1 (widget %2) is not available in launcher forms").arg (u.cls, u.name);
			return false;
		}
	}
	for (const UiWidget &c : u.children)
		if (!CheckClasses (c, err)) return false;
	return true;
}

bool FormRuntime::Call (QJSValue &fn, const Ctx *ctx, const QString &where, bool &failing, QJSValue &out, const QJSValueList &extra)
{
	if (dying || !fn.isCallable ()) return false;
	QJSValueList args = extra;
	args << (ctx ? ctx->item : QJSValue (QJSValue::UndefinedValue)) << QJSValue (ctx ? ctx->index : -1);
	{
		Busy busy (this);
		out = fn.call (args);
	}
	bool interrupted = false;
	if (engine->isInterrupted ()) {
		engine->setInterrupted (false);
		interrupted = true;
	}
	bool error = out.isError ();
	if (engine->hasError ()) {
		out = engine->catchError ();
		error = true;
	}
	if (interrupted || error) {
		if (!failing) Warn (where + ": " + (interrupted ? QString ("stopped after running 2 s") : out.toString ()));
		failing = true;
		return false;
	}
	failing = false;
	return true;
}

void FormRuntime::RunUpdate ()
{
	if (!updateFn.isCallable ()) return;
	QJSValue r;
	if (!Call (updateFn, nullptr, "update()", updateFailing, r)) return;
	QJSValue g = engine->globalObject ();
	QStringList keys;
	if (r.isObject ()) {
		QJSValueIterator it (r);
		while (it.hasNext ()) {
			it.next ();
			const QString k = it.name ();
			if (RESERVED.contains (k)) continue;
			g.setProperty (k, it.value ());
			keys << k;
		}
	}
	for (const QString &k : scopeKeys)
		if (!keys.contains (k)) g.setProperty (k, QJSValue (QJSValue::UndefinedValue));
	scopeKeys = keys;
}

QString FormRuntime::Text (const QJSValue &v)
{
	if (v.isUndefined () || v.isNull ()) return QString ();
	return v.toString ();
}

// ---------------------------------------------------------------------------------------------------------
// building

std::unique_ptr<Node> FormRuntime::Build (const UiWidget &u, QWidget *parent, Node *parentNode, std::shared_ptr<Ctx> ctx, bool inTemplate, QString &err)
{
	QString cls = u.cls;
	if (!KnownClass (cls)) {
		const custom::UiCustom *c = form.Custom (cls);
		if (c && KnownClass (c->extends)) {
			Warn (QString ("%1: class %2 is not available in launcher forms; it is built as %3").arg (u.name, cls, c->extends));
			cls = c->extends;
		} else {
			err = QString ("class %1 (widget %2) is not available in launcher forms").arg (cls, u.name);
			return nullptr;
		}
	}
	QWidget *w = NewWidget (cls, parent);
	auto n = std::make_unique<Node> ();
	n->w = w;
	n->u = &u;
	n->parent = parentNode;
	n->ctx = ctx;
	n->inTemplate = inTemplate;
	nodes[w] = n.get ();
	w->setObjectName (u.name);
	if (!parentNode && !glow) glow = new GlowPainter (w);
	const bool painted = PaintedClass (cls);

	for (const auto &[name, v] : u.dyn) {
		if (RuntimeName (name)) continue;
		if (name == "letterSpacing") {
			SetLetterSpacing (w, v.num);
			continue;
		}
		const QByteArray key = name.toLatin1 ();
		QVariant var = NaturalValue (env, v);
		if (w->metaObject ()->indexOfProperty (key.constData ()) >= 0) var = SafeValue (env, w, key, var); // a Qt property under stdset="0"
		w->setProperty (key.constData (), var);
	}
	for (const auto &[name, v] : u.props) {
		if (name == "currentIndex" && (qobject_cast<QStackedWidget*> (w) || qobject_cast<QTabWidget*> (w))) continue;
		QString why;
		if (!SetStdProperty (env, w, cls, name, v, why) && !painted) Warn (QString ("%1 (%2): property %3: %4").arg (u.name, cls, name, why));
	}
	if (!u.Prop ("focusPolicy")) w->setFocusPolicy (qobject_cast<QLineEdit*> (w) ? Qt::StrongFocus : Qt::NoFocus);
	if (auto *sa = qobject_cast<QScrollArea*> (w); sa && !u.Prop ("focusPolicy")) sa->viewport ()->setFocusPolicy (Qt::NoFocus);

	auto *stack = qobject_cast<QStackedWidget*> (w);
	auto *area = qobject_cast<QScrollArea*> (w);
	auto *tabs = qobject_cast<QTabWidget*> (w);
	const bool isList = (u.Dyn ("list") != nullptr);
	std::vector<QWidget*> built (u.children.size (), nullptr);
	std::vector<bool> isTmpl (u.children.size (), false);
	std::vector<ListInfo::Tmpl> tmpls;
	for (size_t i = 0; i < u.children.size (); i++) {
		const UiWidget &c = u.children[i];
		if (isList && IsTemplate (c)) {
			isTmpl[i] = true;
			ListInfo::Tmpl t;
			t.u = &c;
			t.design = DesignSize (c);
			if (const UiValue *ti = c.Dyn ("templateIf")) t.cond = CompileOnce (*ti, u.name + "/" + c.name + " templateIf", false);
			tmpls.push_back (t);
			continue;
		}
		auto cn = Build (c, w, n.get (), ctx, inTemplate, err);
		if (!cn) { // nothing of this branch stays registered or alive
			Forget (n.get ());
			delete w;
			return nullptr;
		}
		QWidget *cw = cn->w;
		built[i] = cw;
		if (stack) stack->addWidget (cw);
		else if (area && !area->widget ()) {
			area->setWidget (cw);
			if (cn->list && !c.layout.size ()) { // a list without a layout in a scroll area: a virtual grid
				cn->list->mode = ListInfo::VIRTUAL;
				cn->list->area = area;
				Node *ln = cn.get ();
				connect (area->verticalScrollBar (), &QScrollBar::valueChanged, this, [this, ln]() { VirtualUpdate (ln, true); });
				area->viewport ()->installEventFilter (this);
			}
		}
		else if (tabs) {
			const UiValue *title = c.Attr ("title");
			tabs->addTab (cw, title ? title->str : QString ());
		}
		n->kids.push_back (std::move (cn));
	}
	TemplateSlot slot;
	if (!u.layout.empty () && !stack && !area && !tabs) {
		QLayout *lay = MakeLayout (env, u.layout[0], w, built, isTmpl, slot, true);
		const UiValue *fl = u.Dyn ("flow");
		auto *box = qobject_cast<QBoxLayout*> (lay);
		if (fl && fl->type == UiValue::BOOL && fl->flag && box) { // the same items, wrapping
			const int sp = std::max (0, box->spacing ());
			const QMargins m = box->contentsMargins ();
			std::vector<QLayoutItem*> items;
			std::function<void (QLayout*)> take = [&](QLayout *l) {
				while (l->count ()) {
					QLayoutItem *it = l->takeAt (0);
					if (it->widget ()) items.push_back (it);
					else if (QLayout *sub = it->layout ()) { // a flow is flat: the widgets of an inner layout join it
						Warn (u.name + ": a flow takes the widgets of its inner layout " + sub->objectName () + " as its own items");
						take (sub);
						delete sub;
					} else delete it; // spacers: a flow has none
				}
			};
			take (box);
			int insert = 0;
			for (const auto &li : u.layout[0].items) {
				if (li.kind != UiLayoutItem::WIDGET || li.widget < 0) continue;
				if (isTmpl[li.widget]) break;
				insert++;
			}
			delete box;
			auto *flow = new FlowLayout (w, sp, sp);
			flow->setContentsMargins (m);
			for (QLayoutItem *it : items) flow->addItem (it);
			if (slot.insertAt >= 0) slot.insertAt = insert;
		}
	}
	if (u.layout.empty () && !stack && !area && !tabs) {
		for (const UiWidget *c : u.PaintOrder ()) {
			for (size_t i = 0; i < u.children.size (); i++)
				if (&u.children[i] == c && built[i]) built[i]->raise ();
		}
		Anchors *anc = nullptr;
		const QSize ds = DesignSize (u);
		for (size_t i = 0; i < u.children.size (); i++) {
			const UiValue *a = u.children[i].Dyn ("anchor");
			if (!a || !built[i]) continue;
			const int flags = Anchors::Parse (a->str);
			if (!flags) continue;
			if (!ds.isValid ()) {
				Warn (QString ("%1: anchors of its children need its geometry or a fixed size").arg (u.name));
				break;
			}
			const UiValue *g = u.children[i].Prop ("geometry");
			if (!anc) anc = new Anchors (w, ds);
			anc->Add (built[i], flags, g && g->type == UiValue::RECT ? g->rect : built[i]->geometry ());
		}
		if (anc) anc->Apply ();
	}
	if (const UiValue *ci = u.Prop ("currentIndex"); ci && (stack || tabs)) {
		if (stack) stack->setCurrentIndex (ci->Int ());
		else tabs->setCurrentIndex (ci->Int ());
	}
	if (isList) {
		auto L = std::make_unique<ListInfo> ();
		L->where = u.name + " list";
		L->fn = Compile (Src (*u.Dyn ("list")), L->where, false);
		L->templates = std::move (tmpls);
		L->slot = slot;
		if (qobject_cast<QGridLayout*> (w->layout ())) L->mode = ListInfo::GRID;
		else if (!w->layout ()) L->mode = ListInfo::VIRTUAL; // made virtual by its scroll area; elsewhere it shows nothing
		if (L->templates.empty ()) Warn (u.name + ": a list without a template child (dynamic property template = true)");
		const UiValue *fit = u.Dyn ("fit");
		if (L->mode == ListInfo::GRID || (fit && fit->type == UiValue::BOOL && fit->flag)) { // its clones can't widen it
			QSizePolicy sp = w->sizePolicy ();
			auto *box = qobject_cast<QBoxLayout*> (w->layout ());
			const bool vert = (L->mode != ListInfo::GRID && box && (box->direction () == QBoxLayout::TopToBottom || box->direction () == QBoxLayout::BottomToTop));
			if (vert) sp.setVerticalPolicy (QSizePolicy::Ignored);
			else sp.setHorizontalPolicy (QSizePolicy::Ignored);
			w->setSizePolicy (sp);
		}
		n->list = std::move (L);
		w->installEventFilter (this);
	}
	SetupNode (n.get (), u, parentNode && qobject_cast<QStackedWidget*> (parentNode->w));
	return n;
}

void FormRuntime::SetupNode (Node *n, const UiWidget &u, bool)
{
	QWidget *w = n->w;
	const QString base = u.name + " ";
	for (const auto &[name, v] : u.dyn) {
		if (!RuntimeName (name) || name == "list" || name == "templateIf" || name == "scripts") {
			if (name == "tick") n->tick = std::clamp (v.Int (), 0, 60000);
			continue;
		}
		const QString src = Src (v);
		const QString where = base + name;
		if (name == "action") n->action = CompileOnce (v, where, true), n->actionWhere = where;
		else if (name == "doubleAction") n->doubleAction = CompileOnce (v, where, true), n->doubleWhere = where;
		else if (name.startsWith ("key_")) {
			bool ok = false;
			const int key = QMetaEnum::fromType<Qt::Key> ().keyToValue (("Key_" + name.mid (4)).toLatin1 ().constData (), &ok);
			if (!ok) Warn (where + ": Qt has no key " + name.mid (4));
			else if (QJSValue f = CompileOnce (v, where, true); f.isCallable ()) n->keys.push_back ({key, f});
		} else {
			Bind b;
			b.where = where;
			if (name == "bind") b.kind = Bind::MAIN;
			else if (name == "showIf") b.kind = Bind::SHOW;
			else if (name == "enableIf") b.kind = Bind::ENABLE;
			else if (name == "model") b.kind = Bind::MODEL;
			else { b.kind = Bind::PROP; b.prop = name.mid (5).toLatin1 (); }
			b.fn = CompileOnce (v, where, false);
			if (!b.fn.isCallable ()) continue;
			if (b.kind == Bind::MODEL) {
				auto *le = qobject_cast<QLineEdit*> (w);
				static const QRegularExpression member (R"(^\s*[A-Za-z_$][\w$]*(\s*(\.\s*[A-Za-z_$][\w$]*|\[[^\]]+\]))+\s*$)");
				if (!le) { Warn (where + ": model is for QLineEdit"); continue; }
				if (!member.match (src).hasMatch ()) { Warn (where + ": model must be a member expression such as ui.query"); continue; }
				b.setter = CompileOnce (v, where + " (writing)", false, true);
				Node *nn = n;
				connect (le, &QLineEdit::textEdited, this, [this, nn](const QString &t) {
					for (auto &bb : nn->binds)
						if (bb.kind == Bind::MODEL) {
							QJSValue out;
							Call (bb.setter, nn->ctx.get (), bb.where, bb.failing, out, {QJSValue (t)});
						}
					ScheduleRefresh ();
				});
				le->installEventFilter (this);
			}
			n->binds.push_back (b);
		}
	}
	std::stable_sort (n->binds.begin (), n->binds.end (), [](const Bind &a, const Bind &b) { return (a.kind == Bind::SHOW) > (b.kind == Bind::SHOW); });
	if (n->action.isCallable () || n->doubleAction.isCallable ()) {
		if (auto *b = qobject_cast<QAbstractButton*> (w)) {
			connect (b, &QAbstractButton::clicked, this, [this, n]() { RunAction (n, false); });
			if (n->doubleAction.isCallable ()) b->installEventFilter (this);
		} else w->installEventFilter (this);
		w->setAttribute (Qt::WA_Hover);
		if (!u.Prop ("cursor")) w->setCursor (Qt::PointingHandCursor);
	}
	auto boolDyn = [&u](const char *name) {
		const UiValue *v = u.Dyn (name);
		return v && ((v->type == UiValue::BOOL && v->flag) || (v->type == UiValue::STRING && v->str == "true"));
	};
	if (boolDyn ("hover")) w->setAttribute (Qt::WA_Hover);
	if (boolDyn ("clickThrough")) w->setAttribute (Qt::WA_TransparentForMouseEvents);
	if (u.Dyn ("disabledOpacity")) {
		w->installEventFilter (this);
		UpdateDisabled (w);
	}
	if (u.Dyn ("glowRings") && glow) glow->Add (w);
	if (qobject_cast<Painted*> (w)) RegisterPainted (n);
	if (u.name == "toast" && !n->inTemplate) {
		toastLabel = w;
		w->hide ();
	}
	if (n->tick > 0) {
		const int ms = std::max (50, n->tick);
		n->tick = ms;
		if (!ticks.count (ms)) {
			auto *t = new QTimer (this);
			t->setInterval (ms);
			connect (t, &QTimer::timeout, this, [this, ms]() { TickNow (ms); });
			ticks[ms] = t;
			SyncTimers ();
		}
	}
}

void FormRuntime::RegisterPainted (Node *n)
{
	auto *p = static_cast<Painted*> (n->w.data ());
	p->setActive (active);
	std::erase_if (painted, [](const QPointer<QWidget> &q) { return q.isNull (); }); // clones that went
	painted.push_back (p);
	if (n->inTemplate || n->u->name.isEmpty ()) return;
	if (QObject *so = p->ScriptObject ()) {
		QJSEngine::setObjectOwnership (so, QJSEngine::CppOwnership);
		wObj.setProperty (n->u->name, engine->newQObject (so));
		const QMetaObject *mo = so->metaObject ();
		const int slot = metaObject ()->indexOfSlot ("ScriptSignal()");
		for (int i = QObject::staticMetaObject.methodCount (); i < mo->methodCount (); i++)
			if (mo->method (i).methodType () == QMetaMethod::Signal) QMetaObject::connect (so, i, this, slot);
	}
}

void FormRuntime::Forget (Node *n)
{
	if (!n) return;
	nodes.erase (n->w.data ());
	for (auto &k : n->kids) Forget (k.get ());
	if (n->list)
		for (auto &c : n->list->clones) Forget (c.node.get ());
}

std::unique_ptr<Node> FormRuntime::MakeClone (Node *owner, int tmpl, const QJSValue &item, int index)
{
	auto ctx = std::make_shared<Ctx> ();
	ctx->item = item;
	ctx->index = index;
	QString err;
	auto c = Build (*owner->list->templates[tmpl].u, owner->w, owner, ctx, true, err);
	if (!c) {
		Warn (owner->u->name + ": " + err);
		return nullptr;
	}
	return c;
}

// ---------------------------------------------------------------------------------------------------------
// refresh

void FormRuntime::ScheduleRefresh ()
{
	if (dying || queued) return;
	queued = true;
	QMetaObject::invokeMethod (this, [this]() { if (queued) Refresh (); }, Qt::QueuedConnection);
}

void FormRuntime::Refresh ()
{
	queued = false;
	if (dying || refreshing || !rootNode) return;
	Busy busy (this); // getters, proxies and conversions run JS too
	refreshing = true;
	RunUpdate ();
	Walk (rootNode.get ());
	refreshing = false;
}

void FormRuntime::Walk (Node *n)
{
	QWidget *w = n->w;
	if (!w) return;
	for (auto &b : n->binds)
		if (b.kind == Bind::SHOW) Apply (n, b, true);
	if (w->isHidden ()) return;
	for (auto &b : n->binds)
		if (b.kind != Bind::SHOW) Apply (n, b, false);
	if (n->list) UpdateList (n);
	for (auto &k : n->kids) Walk (k.get ());
	if (n->list)
		for (auto &c : n->list->clones)
			if (c.node && (n->list->mode != ListInfo::VIRTUAL || c.index >= 0)) Walk (c.node.get ());
}

void FormRuntime::Apply (Node *n, Bind &b, bool)
{
	QJSValue v;
	if (!Call (b.fn, n->ctx.get (), b.where, b.failing, v)) return;
	QWidget *w = n->w;
	if (b.kind == Bind::MODEL) {
		auto *le = qobject_cast<QLineEdit*> (w);
		const QString t = Text (v);
		if (le && le->text () != t) le->setText (t);
		return;
	}
	if (b.kind == Bind::SHOW) { // against the widget: lists show and hide clones too
		SetShown (n, v.toBool ());
		return;
	}
	const QVariant var = v.toVariant ();
	if (b.have && var == b.last && var.isValid ()) return;
	b.have = true;
	b.last = var;
	switch (b.kind) {
	case Bind::ENABLE: if (w->isEnabled () != v.toBool ()) w->setEnabled (v.toBool ()); break;
	case Bind::MAIN: ApplyMain (n, v); break;
	case Bind::PROP: ApplyProp (n, b.prop, v); break;
	default: break;
	}
}

void FormRuntime::ApplyMain (Node *n, const QJSValue &v)
{
	QWidget *w = n->w;
	if (auto *st = qobject_cast<QStackedWidget*> (w)) {
		QWidget *target = nullptr;
		const QString s = Text (v);
		for (int i = 0; i < st->count () && !target; i++)
			if (st->widget (i)->property ("page").toString () == s) target = st->widget (i);
		if (!target && v.isNumber ()) target = st->widget (v.toInt ());
		if (!target && st->count ()) target = st->widget (0);
		if (target && st->currentWidget () != target) {
			QWidget *f = QApplication::focusWidget ();
			const bool keep = f && root && root->isAncestorOf (f) && !st->isAncestorOf (f); // a search box outside the pages
			st->setCurrentWidget (target);
			if (onPageChange && !keep) onPageChange ();
		}
	} else if (auto *b = qobject_cast<QAbstractButton*> (w)) {
		if (b->isCheckable ()) { if (b->isChecked () != v.toBool ()) b->setChecked (v.toBool ()); }
		else if (b->text () != Text (v)) b->setText (Text (v));
	} else if (auto *l = qobject_cast<QLabel*> (w)) {
		const QString t = SafeText (env, Text (v));
		if (l->text () != t) {
			l->setText (t);
			if (l->property ("autoSize").toBool () && !(l->parentWidget () && l->parentWidget ()->layout ())) {
				l->adjustSize ();
				if (QWidget *p = l->parentWidget ())
					if (auto *a = p->findChild<Anchors*> (QString (), Qt::FindDirectChildrenOnly)) a->Apply ();
			}
		}
	} else if (auto *le = qobject_cast<QLineEdit*> (w)) {
		if (le->text () != Text (v)) le->setText (Text (v));
	} else if (auto *pb = qobject_cast<QProgressBar*> (w)) {
		pb->setValue (v.toInt ());
	} else if (auto *gb = qobject_cast<QGroupBox*> (w)) {
		gb->setTitle (Text (v));
	} else Warn (n->u->name + ": bind does nothing on " + n->u->cls);
}

void FormRuntime::ApplyProp (Node *n, const QByteArray &prop, const QJSValue &v)
{
	QWidget *w = n->w;
	if (prop == "fixedWidth") { w->setFixedWidth (std::max (0, v.toInt ())); return; }
	if (prop == "fixedHeight") { w->setFixedHeight (std::max (0, v.toInt ())); return; }
	if (prop == "layoutMargins" || prop == "layoutSpacing") {
		QLayout *l = w->layout ();
		if (!l) return;
		if (prop == "layoutSpacing") { l->setSpacing (v.toInt ()); return; }
		if (v.isArray ()) l->setContentsMargins (v.property (0).toInt (), v.property (1).toInt (), v.property (2).toInt (), v.property (3).toInt ());
		else l->setContentsMargins (v.toInt (), v.toInt (), v.toInt (), v.toInt ());
		return;
	}
	if (prop == "letterSpacing") { SetLetterSpacing (w, v.toNumber ()); return; }
	const QMetaObject *mo = w->metaObject ();
	const int idx = mo->indexOfProperty (prop.constData ());
	if (idx >= 0) {
		const QMetaProperty mp = mo->property (idx);
		QVariant var = v.toVariant ();
		if (mp.isEnumType () && var.typeId () == QMetaType::QString) {
			bool ok = false;
			const int e = mp.enumerator ().keysToValue (custom::UiSetNames (var.toString ()).join ('|').toLatin1 ().constData (), &ok);
			if (ok) var = e;
		}
		var = SafeValue (env, w, prop, var);
		if (!w->setProperty (prop.constData (), var)) Warn (n->u->name + ": bind_" + QString::fromLatin1 (prop) + ": the value doesn't fit");
		return;
	}
	const QVariant var = v.toVariant ();
	if (w->property (prop.constData ()) == var) return;
	w->setProperty (prop.constData (), var);
	if (prop.startsWith ("glow") || prop == "disabledOpacity") {
		if (glow) glow->Changed (w);
		if (prop == "disabledOpacity") UpdateDisabled (w);
		return;
	}
	if (prop == "maxItems" || prop == "cellWidth" || prop == "cellHeight" || prop == "columns" || prop == "fit") {
		if (n->list) n->list->have = false;
		return;
	}
	Repolish (w);
}

void FormRuntime::SetShown (Node *n, bool on)
{
	QWidget *w = n->w;
	if (!w) return;
	if (n->wantShown != on) {
		n->wantShown = on;
		if (n->parent && n->parent->list && n->parent->w && n->parent->w->property ("fit").toBool ()) ScheduleRefresh (); // the fit counts again
	}
	on = on && !n->fitHidden;
	bool shown = !w->isHidden ();
	if (auto *a = w->findChild<QPropertyAnimation*> ("formsFade", Qt::FindDirectChildrenOnly); a && a->state () == QAbstractAnimation::Running)
		shown = (a->endValue ().toDouble () > 0.5); // where a running fade ends
	if (shown == on) return;
	const int fade = w->property ("fade").toInt ();
	if (fade > 0 && view && view->isVisible ()) Fade (w, on, fade);
	else {
		if (auto *a = w->findChild<QPropertyAnimation*> ("formsFade", Qt::FindDirectChildrenOnly)) {
			a->stop ();
			delete a;
			w->setGraphicsEffect (nullptr);
		}
		w->setVisible (on);
		UpdateDisabled (w);
	}
}

void FormRuntime::Fade (QWidget *w, bool in, int ms)
{
	if (!w) return;
	if (auto *old = w->findChild<QPropertyAnimation*> ("formsFade", Qt::FindDirectChildrenOnly)) {
		old->stop ();
		delete old;
	}
	auto *e = qobject_cast<QGraphicsOpacityEffect*> (w->graphicsEffect ());
	if (!e) {
		e = new QGraphicsOpacityEffect (w);
		e->setOpacity (in ? 0.0 : 1.0);
		w->setGraphicsEffect (e);
	}
	if (in) w->show ();
	auto *a = new QPropertyAnimation (e, "opacity", w);
	a->setObjectName ("formsFade");
	a->setDuration (std::clamp (ms, 1, 5000));
	a->setStartValue (e->opacity ());
	a->setEndValue (in ? 1.0 : 0.0);
	QPointer<QWidget> pw (w);
	connect (a, &QPropertyAnimation::finished, this, [this, pw, in, a]() {
		a->setObjectName (QString ()); // done: UpdateDisabled may set the effect again
		a->deleteLater ();
		if (!pw) return;
		pw->setGraphicsEffect (nullptr);
		if (!in) pw->hide ();
		UpdateDisabled (pw);
	});
	a->start ();
}

void FormRuntime::UpdateDisabled (QWidget *w)
{
	if (!w) return;
	bool ok = false;
	const double o = w->property ("disabledOpacity").toDouble (&ok);
	auto *e = qobject_cast<QGraphicsOpacityEffect*> (w->graphicsEffect ());
	if (auto *a = w->findChild<QPropertyAnimation*> ("formsFade", Qt::FindDirectChildrenOnly); a && a->state () == QAbstractAnimation::Running) return; // a fade owns the effect
	if (ok && o < 1.0 && !w->isEnabled ()) {
		if (!e) {
			e = new QGraphicsOpacityEffect (w);
			w->setGraphicsEffect (e);
		}
		e->setOpacity (std::clamp (o, 0.0, 1.0));
	} else if (e) w->setGraphicsEffect (nullptr);
}

void FormRuntime::RunAction (Node *n, bool dbl)
{
	QJSValue &fn = (dbl ? n->doubleAction : n->action);
	if (!fn.isCallable () || dying) return;
	QJSValue out;
	Call (fn, n->ctx.get (), dbl ? n->doubleWhere : n->actionWhere, n->actionFailing, out);
	ScheduleRefresh ();
}

// ---------------------------------------------------------------------------------------------------------
// lists

int FormRuntime::PickTemplate (ListInfo &L, const QJSValue &item, int index)
{
	for (size_t t = 0; t < L.templates.size (); t++) {
		auto &T = L.templates[t];
		if (!T.cond.isCallable ()) return (int)t;
		Ctx c;
		c.item = item;
		c.index = index;
		QJSValue out;
		bool failing = false;
		if (Call (T.cond, &c, L.where + " templateIf", failing, out) && out.toBool ()) return (int)t;
	}
	return -1;
}

void FormRuntime::UpdateList (Node *n)
{
	ListInfo &L = *n->list;
	QJSValue arr;
	if (!Call (L.fn, n->ctx.get (), L.where, L.failing, arr)) return;
	int len = 0;
	if (arr.isArray () || arr.property ("length").isNumber ()) len = std::max (0, arr.property ("length").toInt ());
	const int maxItems = n->w->property ("maxItems").toInt ();
	if (maxItems > 0) len = std::min (len, maxItems);
	const int cap = (L.mode == ListInfo::VIRTUAL ? 1000000 : 2000); // a box or grid makes a widget per item
	if (len > cap) {
		if (!L.capped) Warn (QString ("%1: more than %2 items, the rest are left out").arg (L.where).arg (cap));
		L.capped = true;
		len = cap;
	}
	const QVariant v = arr.toVariant ();
	const bool same = (L.have && v == L.last && len == L.length);
	L.array = arr;
	L.length = len;
	L.last = v;
	L.have = true;
	if (L.mode == ListInfo::VIRTUAL) {
		VirtualUpdate (n, false);
		return;
	}
	for (int i = 0; i < len; i++) {
		QJSValue item = arr.property (i);
		if (same && i < (int)L.clones.size ()) { // the same data: the clones get this refresh's objects
			if (L.clones[i].node) L.clones[i].node->ctx->item = item;
			continue;
		}
		const int t = PickTemplate (L, item, i);
		if (i < (int)L.clones.size () && L.clones[i].tmpl == t && L.clones[i].node) {
			L.clones[i].node->ctx->item = item;
			L.clones[i].node->ctx->index = i;
			continue;
		}
		if (i >= (int)L.clones.size ()) L.clones.emplace_back ();
		ListInfo::Clone &c = L.clones[i];
		if (c.node) {
			Forget (c.node.get ());
			QWidget *old = c.node->w;
			c.node.reset ();
			delete old;
		}
		c.tmpl = t;
		c.index = i;
		if (t < 0) continue;
		c.node = MakeClone (n, t, item, i);
		if (!c.node) continue;
		QWidget *cw = c.node->w;
		QLayout *lay = n->w->layout ();
		int at = std::max (0, L.slot.insertAt);
		for (int k = 0; k < i; k++) if (L.clones[k].node) at++;
		if (auto *box = qobject_cast<QBoxLayout*> (lay)) box->insertWidget (std::min (at, box->count ()), cw, 0, (Qt::Alignment)L.slot.align);
		else if (auto *flow = dynamic_cast<FlowLayout*> (lay)) flow->insertWidgetAt (at, cw);
		cw->show ();
	}
	while ((int)L.clones.size () > len) {
		ListInfo::Clone &c = L.clones.back ();
		if (c.node) {
			Forget (c.node.get ());
			QWidget *old = c.node->w;
			c.node.reset ();
			delete old;
		}
		L.clones.pop_back ();
	}
	PlaceList (n);
}

void FormRuntime::PlaceList (Node *n)
{
	ListInfo &L = *n->list;
	QWidget *c = n->w;
	if (L.mode == ListInfo::GRID) {
		auto *grid = qobject_cast<QGridLayout*> (c->layout ());
		if (!grid) return;
		const int cellW = c->property ("cellWidth").toInt ();
		int cols = c->property ("columns").toInt ();
		if (cellW > 0) {
			const QMargins m = grid->contentsMargins ();
			const int W = c->width () - m.left () - m.right ();
			const int hs = std::max (0, grid->horizontalSpacing ());
			cols = std::max (1, (W + hs) / cellW);
		}
		cols = std::clamp (cols, 1, 64);
		const bool changed = (cols != L.columns);
		int k = 0;
		for (auto &cl : L.clones) {
			if (!cl.node || !cl.node->w) continue;
			QWidget *w = cl.node->w;
			const int r = L.slot.row + k / cols, col = L.slot.col + k % cols;
			int ir = -1, ic = -1, rs, cs;
			const int idx = grid->indexOf (w);
			if (idx >= 0) grid->getItemPosition (idx, &ir, &ic, &rs, &cs);
			if (changed || ir != r || ic != col) {
				grid->removeWidget (w);
				grid->addWidget (w, r, col, (Qt::Alignment)L.slot.align);
			}
			k++;
		}
		const int need = (k > 0 && k < cols ? cols - k : 0);
		while ((int)L.fillers.size () > need) {
			if (QWidget *f = L.fillers.back ()) { grid->removeWidget (f); f->deleteLater (); }
			L.fillers.pop_back ();
		}
		for (int i = 0; i < need; i++) {
			if (i == (int)L.fillers.size ()) {
				auto *f = new QWidget (c);
				f->setObjectName ("listFiller");
				f->setAttribute (Qt::WA_TransparentForMouseEvents);
				f->setFocusPolicy (Qt::NoFocus);
				f->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Ignored);
				L.fillers.push_back (f);
			}
			QWidget *f = L.fillers[i];
			if (!f) continue;
			const int col = L.slot.col + k + i;
			int ir = -1, ic = -1, rs, cs;
			const int idx = grid->indexOf (f);
			if (idx >= 0) grid->getItemPosition (idx, &ir, &ic, &rs, &cs);
			if (ir != L.slot.row || ic != col) {
				grid->removeWidget (f);
				grid->addWidget (f, L.slot.row, col);
			}
			f->show ();
		}
		for (int i = 0; i < std::max (cols, L.columns); i++) grid->setColumnStretch (L.slot.col + i, i < cols ? 1 : 0);
		L.columns = cols;
		return;
	}
	if (!c->property ("fit").toBool ()) return;
	auto *box = qobject_cast<QBoxLayout*> (c->layout ());
	if (!box) return;
	const QMargins m = box->contentsMargins ();
	const bool horiz = (box->direction () == QBoxLayout::LeftToRight || box->direction () == QBoxLayout::RightToLeft);
	const int avail = (horiz ? c->width () - m.left () - m.right () : c->height () - m.top () - m.bottom ());
	const int sp = std::max (0, box->spacing ());
	int used = 0, shown = 0;
	bool any = false;
	for (auto &cl : L.clones) {
		if (!cl.node || !cl.node->w) continue;
		QWidget *w = cl.node->w;
		if (!cl.node->wantShown) { // its showIf hides it: it takes no room
			cl.node->fitHidden = false;
			continue;
		}
		const QSize h = w->sizeHint ().expandedTo (w->minimumSize ());
		const int need = used + (shown ? sp : 0) + (horiz ? h.width () : h.height ());
		const bool fits = (need <= avail);
		cl.node->fitHidden = !fits;
		if (w->isHidden () == fits) { w->setVisible (fits); any = true; }
		if (fits) {
			used = need;
			shown++;
		}
	}
	if (any && !refreshing) ScheduleRefresh ();
}

void FormRuntime::VirtualUpdate (Node *n, bool walk)
{
	ListInfo &L = *n->list;
	QWidget *content = n->w;
	QScrollArea *sa = L.area;
	if (!sa || !content || dying) return;
	Busy busy (this);
	const QSize d0 = (!L.templates.empty () && L.templates[0].design.isValid () ? L.templates[0].design : QSize (100, 100));
	int cellW = content->property ("cellWidth").toInt (), cellH = content->property ("cellHeight").toInt ();
	if ((cellW <= 0 || cellH <= 0) && !L.cellWarned) {
		Warn (n->u->name + ": a grid in a scroll area needs cellWidth and cellHeight; the template's size is used");
		L.cellWarned = true;
	}
	cellW = std::clamp (cellW > 0 ? cellW : d0.width (), 8, 4096);
	cellH = std::clamp (cellH > 0 ? cellH : d0.height (), 8, 4096);
	const int W = sa->viewport ()->width (), VH = sa->viewport ()->height ();
	const int cols = std::clamp (W / cellW, 1, 64);
	const int cw = std::max (1, W / cols);
	const qint64 rows = (L.length + cols - 1) / cols;
	const int height = (int)std::min<qint64> (rows * cellH, QWIDGETSIZE_MAX);
	if (content->minimumHeight () != height) content->setMinimumHeight (height);
	const int y0 = sa->verticalScrollBar ()->value ();
	const int first = std::max (0, y0 / cellH) * cols;
	const int last = std::min (L.length, (std::max (0, (y0 + VH) / cellH) + 2) * cols); // the rows in view and one more
	for (auto &cl : L.clones)
		if (cl.index >= 0 && (cl.index < first || cl.index >= last)) {
			cl.index = -1;
			if (cl.node && cl.node->w) cl.node->w->hide ();
		}
	std::vector<Node*> fresh;
	for (int i = first; i < last; i++) {
		QJSValue item = L.array.property (i);
		const int t = PickTemplate (L, item, i);
		int use = -1;
		for (size_t k = 0; k < L.clones.size () && use < 0; k++)
			if (t >= 0 && L.clones[k].index == i && L.clones[k].tmpl == t && L.clones[k].node) use = (int)k;
		for (size_t k = 0; k < L.clones.size () && use < 0; k++)
			if (L.clones[k].index == i && L.clones[k].tmpl != t) { // another template now, or none
				L.clones[k].index = -1;
				if (L.clones[k].node && L.clones[k].node->w) L.clones[k].node->w->hide ();
			}
		if (t < 0) continue;
		for (size_t k = 0; k < L.clones.size () && use < 0; k++)
			if (L.clones[k].index < 0 && L.clones[k].tmpl == t && L.clones[k].node) use = (int)k;
		if (use < 0) {
			auto node = MakeClone (n, t, item, i);
			if (!node) continue;
			L.clones.emplace_back ();
			L.clones.back ().tmpl = t;
			L.clones.back ().node = std::move (node);
			use = (int)L.clones.size () - 1;
		}
		ListInfo::Clone &cl = L.clones[use];
		const bool moved = (cl.index != i);
		cl.index = i;
		cl.node->ctx->item = item;
		cl.node->ctx->index = i;
		const QSize d = L.templates[t].design.isValid () ? L.templates[t].design : cl.node->w->sizeHint ();
		const int cardW = std::max (1, cw - (cellW - d.width ())), cardH = std::max (1, d.height ());
		cl.node->w->setGeometry ((i % cols) * cw, (i / cols) * cellH, cardW, cardH);
		if (cl.node->w->isHidden () && cl.node->wantShown) cl.node->w->show ();
		if (moved) fresh.push_back (cl.node.get ()); // the others keep their bindings
	}
	if (walk && !refreshing)
		for (Node *f : fresh) Walk (f);
}

// ---------------------------------------------------------------------------------------------------------
// events, keys, timers, toast

bool FormRuntime::eventFilter (QObject *obj, QEvent *e)
{
	if (dying) return false;
	auto it = nodes.find (obj);
	Node *n = (it != nodes.end () ? it->second : nullptr);
	switch (e->type ()) {
	case QEvent::MouseButtonPress:
		if (n && !qobject_cast<QAbstractButton*> (obj) && (n->action.isCallable () || n->doubleAction.isCallable ())
			&& static_cast<QMouseEvent*> (e)->button () == Qt::LeftButton) {
			n->pressed = true;
			return true;
		}
		break;
	case QEvent::MouseButtonRelease:
		if (n && n->pressed && static_cast<QMouseEvent*> (e)->button () == Qt::LeftButton) {
			n->pressed = false;
			if (QRect (QPoint (0, 0), n->w->size ()).contains (static_cast<QMouseEvent*> (e)->position ().toPoint ())) RunAction (n, false);
			return true;
		}
		break;
	case QEvent::MouseButtonDblClick:
		if (n && static_cast<QMouseEvent*> (e)->button () == Qt::LeftButton) {
			if (n->doubleAction.isCallable ()) {
				n->pressed = false;
				RunAction (n, true);
				return true;
			}
			if (n->action.isCallable () && !qobject_cast<QAbstractButton*> (obj)) {
				n->pressed = true; // a quick second click is a click
				return true;
			}
		}
		break;
	case QEvent::KeyPress:
		if (auto *le = qobject_cast<QLineEdit*> (obj); le && n && static_cast<QKeyEvent*> (e)->key () == Qt::Key_Escape) {
			for (auto &b : n->binds)
				if (b.kind == Bind::MODEL) {
					le->clear ();
					QJSValue out;
					Call (b.setter, n->ctx.get (), b.where, b.failing, out, {QJSValue (QString ())});
					ScheduleRefresh ();
					return true;
				}
		}
		break;
	case QEvent::EnabledChange:
		if (n && n->u->Dyn ("disabledOpacity")) UpdateDisabled (n->w);
		break;
	case QEvent::Resize:
		if (n && n->list && n->list->mode != ListInfo::VIRTUAL) PlaceList (n);
		else if (!n) { // a scroll area's viewport; VirtualUpdate changes the node map, so collect first
			std::vector<Node*> grids;
			for (auto &[o, nn] : nodes)
				if (nn->list && nn->list->mode == ListInfo::VIRTUAL && nn->list->area && nn->list->area->viewport () == obj) grids.push_back (nn);
			for (Node *g : grids) VirtualUpdate (g, true);
		}
		break;
	default:
		break;
	}
	return false;
}

bool FormRuntime::HandleKey (QKeyEvent *e)
{
	if (dying || !root) return false;
	if (e->modifiers () & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return false;
	if (queued && !refreshing) Refresh (); // typing and Return in one batch: the handler sees what was typed
	auto run = [this, e](Node *n) {
		for (auto &[k, fn] : n->keys)
			if (k == e->key ()) {
				QJSValue out;
				bool failing = false;
				Call (fn, n->ctx.get (), n->u->name + " key", failing, out);
				ScheduleRefresh ();
				return true;
			}
		return false;
	};
	QWidget *f = QApplication::focusWidget ();
	if (!f || (f != root && !root->isAncestorOf (f))) f = root;
	for (QWidget *w = f; w; w = w->parentWidget ()) { // the focus and its parents first
		auto it = nodes.find (w);
		if (it != nodes.end () && w->isVisible () && run (it->second)) return true;
		if (w == root) break;
	}
	std::function<bool (Node*)> visit = [&](Node *n) -> bool { // then the shown widgets in form order (the page shown)
		if (!n->w || n->w->isHidden ()) return false;
		if (run (n)) return true;
		for (auto &k : n->kids)
			if (visit (k.get ())) return true;
		return false;
	};
	return visit (rootNode.get ());
}

void FormRuntime::TickNow (int interval)
{
	if (dying || !active || refreshing) return;
	Busy busy (this);
	std::vector<Node*> due;
	for (auto &[o, n] : nodes)
		if ((interval == 0 ? n->tick > 0 : n->tick == interval) && n->w && n->w->isVisible ()) due.push_back (n);
	for (Node *n : due)
		for (auto &b : n->binds) Apply (n, b, false);
}

void FormRuntime::SyncTimers ()
{
	for (auto &[ms, t] : ticks) {
		if (active && !dying && !t->isActive ()) t->start ();
		else if ((!active || dying) && t->isActive ()) t->stop ();
	}
}

void FormRuntime::SetActive (bool on)
{
	if (on == active) return;
	active = on;
	for (auto &p : painted)
		if (p) static_cast<Painted*> (p.data ())->setActive (on);
	SyncTimers ();
	if (on) TickNow (0); // every tick widget: the clock jumps to now
}

void FormRuntime::LauncherSignal ()
{
	if (dying) return;
	revision++;
	QObject *s = sender ();
	const int idx = senderSignalIndex ();
	if (s && idx >= 0) {
		const QByteArray name = s->metaObject ()->method (idx).name ();
		if (name == "activeChanged") SetActive (s->property ("active").toBool ());
		else if (name == "returnedFromClassic" && onPageChange) onPageChange ();
	}
	ScheduleRefresh ();
}

void FormRuntime::ScriptSignal ()
{
	ScheduleRefresh ();
}

QWidget *FormRuntime::Find (const QString &name) const
{
	if (name.isEmpty ()) return nullptr;
	for (const auto &[o, n] : nodes)
		if (!n->inTemplate && n->w && n->u && n->u->name == name) return n->w;
	return nullptr;
}

void FormRuntime::Toast (const QString &text)
{
	auto *l = qobject_cast<QLabel*> (toastLabel.data ());
	if (!l) {
		if (log) log ("toast: " + text);
		return;
	}
	l->setText (SafeText (env, text));
	l->adjustSize ();
	if (QWidget *p = l->parentWidget ())
		if (auto *a = p->findChild<Anchors*> (QString (), Qt::FindDirectChildrenOnly)) a->Apply ();
	l->raise ();
	Fade (l, true, 200);
	toastTimer.start ();
}

}
