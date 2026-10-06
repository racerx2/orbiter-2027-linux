
// custom: forms skins; a built form with its JavaScript: bindings, actions, lists, keys, ticks, toast

#ifndef __FORMS_FORMRUNTIME_H
#define __FORMS_FORMRUNTIME_H

#include "FormBuild.h"
#include <QJSEngine>
#include <QJSValue>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

class QKeyEvent;
class QScrollArea;

namespace forms {

	class GlowPainter;
	class FormRuntime;

	// what JS sees as `view`
	class ViewApi: public QObject {
		Q_OBJECT
		Q_PROPERTY(int width READ width)
		Q_PROPERTY(int height READ height)
		Q_PROPERTY(int revision READ revision)
	public:
		explicit ViewApi (FormRuntime *rt);
		int width () const;
		int height () const;
		int revision () const;
		Q_INVOKABLE void toast (const QString &text);
		Q_INVOKABLE bool hasFocus (const QString &name) const;
		Q_INVOKABLE void focus (const QString &name);
	private:
		FormRuntime *rt;
	};

	struct Ctx {
		QJSValue item;
		int index = -1;
	};

	struct Bind {
		enum Kind { MAIN, PROP, SHOW, ENABLE, MODEL };
		Kind kind = MAIN;
		QByteArray prop;
		QJSValue fn, setter;
		QString where;
		bool failing = false;
		bool have = false;
		QVariant last;
	};

	struct ListInfo;

	struct Node {
		QPointer<QWidget> w;
		const UiWidget *u = nullptr;
		Node *parent = nullptr;
		std::vector<std::unique_ptr<Node>> kids;
		std::vector<Bind> binds;
		QJSValue action, doubleAction;
		QString actionWhere, doubleWhere;
		bool actionFailing = false;
		std::vector<std::pair<int, QJSValue>> keys;
		std::unique_ptr<ListInfo> list;
		std::shared_ptr<Ctx> ctx;
		int tick = 0;
		bool pressed = false;
		bool inTemplate = false;
		bool wantShown = true;  // what showIf says
		bool fitHidden = false; // a fit list hides it: it doesn't fit
		~Node ();
	};

	struct ListInfo {
		QJSValue fn;
		QString where;
		bool failing = false;
		struct Tmpl { const UiWidget *u; QJSValue cond; QSize design; };
		std::vector<Tmpl> templates;
		TemplateSlot slot;
		enum Mode { BOX, GRID, VIRTUAL } mode = BOX;
		QVariant last;
		bool have = false;
		QJSValue array;
		int length = 0;
		struct Clone { std::unique_ptr<Node> node; int tmpl = -1; int index = -1; };
		std::vector<Clone> clones; // BOX, GRID: by position; VIRTUAL: a pool, index -1 when free
		int columns = 0;
		bool capped = false, cellWarned = false;
		std::vector<QPointer<QWidget>> fillers; // GRID: keeps the spacing of columns a short first row leaves empty
		QPointer<QScrollArea> area;
	};

	class FormRuntime: public QObject {
		Q_OBJECT
	public:
		FormRuntime (QWidget *view, QObject *launcher, std::function<void (const QString &)> log);
		~FormRuntime ();
		bool Load (const QString &formFile, const QString &skinDir, QString &err);
		QWidget *Root () const { return root; }
		QWidget *View () const { return view; }
		QStringList Files () const;              // the form, its scripts and qrc files, for the reload watcher
		void Shutdown ();
		void ScheduleRefresh ();
		void Refresh ();
		bool HandleKey (QKeyEvent *e);
		void SetActive (bool on);
		void Toast (const QString &text);
		QWidget *Find (const QString &name) const; // outside templates
		int Revision () const { return revision; }
		int Warnings () const { return warnings; }
		QJSEngine *Engine () const { return engine; }
		std::function<void ()> onPageChange;     // the view takes the focus

	protected:
		bool eventFilter (QObject *obj, QEvent *e) override;

	private slots:
		void LauncherSignal ();
		void ScriptSignal ();

	private:
		struct Busy { // the watchdog's clock runs while JS may run
			FormRuntime *r;
			explicit Busy (FormRuntime *rt): r (rt) { r->BeginBusy (); }
			~Busy () { r->EndBusy (); }
		};
		void BeginBusy ();
		void EndBusy ();
		void Warn (const QString &s);
		bool LoadScripts (QString &err);
		bool CheckClasses (const UiWidget &u, QString &err) const;
		QJSValue Compile (const QString &src, const QString &where, bool statement, bool setter = false);
		QJSValue CompileOnce (const UiValue &v, const QString &where, bool statement, bool setter = false); // clones share the function
		bool Call (QJSValue &fn, const Ctx *ctx, const QString &where, bool &failing, QJSValue &out, const QJSValueList &extra = {});
		void RunUpdate ();
		std::unique_ptr<Node> Build (const UiWidget &u, QWidget *parent, Node *parentNode, std::shared_ptr<Ctx> ctx, bool inTemplate, QString &err);
		void SetupNode (Node *n, const UiWidget &u, bool isPage);
		void Walk (Node *n);
		void Apply (Node *n, Bind &b, bool showOnly);
		void ApplyMain (Node *n, const QJSValue &v);
		void ApplyProp (Node *n, const QByteArray &prop, const QJSValue &v);
		void SetShown (Node *n, bool on);
		void RunAction (Node *n, bool dbl);
		void UpdateList (Node *n);
		void PlaceList (Node *n);
		void VirtualUpdate (Node *n, bool walk);
		int PickTemplate (ListInfo &L, const QJSValue &item, int index);
		std::unique_ptr<Node> MakeClone (Node *owner, int tmpl, const QJSValue &item, int index);
		void Forget (Node *n);
		void RegisterPainted (Node *n);
		void TickNow (int interval);
		void SyncTimers ();
		void Fade (QWidget *w, bool in, int ms);
		void UpdateDisabled (QWidget *w);
		static QString Text (const QJSValue &v);

		QPointer<QWidget> view;
		QPointer<QObject> launcher;
		std::function<void (const QString &)> log;
		UiForm form;
		BuildEnv env;
		QJSEngine *engine = nullptr;
		ViewApi *viewApi = nullptr;
		QObject *orbitsApi = nullptr;
		QJSValue updateFn, wObj;
		QStringList scopeKeys, scriptFiles;
		QPointer<QWidget> root;
		std::unique_ptr<Node> rootNode;
		std::unordered_map<QObject*, Node*> nodes;
		std::map<std::pair<const void*, int>, QJSValue> compiled;
		GlowPainter *glow = nullptr;
		std::map<int, QTimer*> ticks;
		std::vector<QPointer<QWidget>> painted;
		QPointer<QWidget> toastLabel;
		QTimer toastTimer;
		bool active = false, dying = false, refreshing = false, queued = false;
		int revision = 0, warnings = 0;
		std::shared_ptr<Ctx> rootCtx;

		// watchdog: interrupts a JS call that runs too long
		std::thread watchdog;
		std::mutex wdMutex;
		std::condition_variable wdCond;
		bool wdStop = false;
		std::atomic<qint64> busySince {0};
		std::atomic<QJSEngine*> wdEngine {nullptr};
		bool updateFailing = false;
		int depth = 0;
	};

}

#endif // !__FORMS_FORMRUNTIME_H
