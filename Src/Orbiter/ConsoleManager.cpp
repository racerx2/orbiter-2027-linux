#include "ConsoleManager.h"

#include <unistd.h>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QThread>
#include <QVBoxLayout>
#include <QWindow>

// the console window of a Windows console program: output above, input line below
class ConsoleWnd : public QWidget {
public:
    ConsoleWnd();
    QPlainTextEdit* out;
    QLineEdit* in;
protected:
    void closeEvent(QCloseEvent* e) override { e->ignore(); } // no SC_CLOSE: "exit" ends the session
};

static ConsoleWnd* s_wnd = nullptr; // lives until the process ends, as the console does
static void (*s_input)(const char* line) = nullptr;

ConsoleWnd::ConsoleWnd()
    : QWidget(nullptr, Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowMinimizeButtonHint)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    out = new QPlainTextEdit(this);
    out->setReadOnly(true);
    out->setFont(font);
    out->setMaximumBlockCount(9001); // console screen buffer height
    in = new QLineEdit(this);
    in->setFont(font);
    QLabel* prompt = new QLabel("> ", this);
    prompt->setFont(font);
    QHBoxLayout* row = new QHBoxLayout;
    row->addWidget(prompt);
    row->addWidget(in);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(out);
    layout->addLayout(row);
    setFocusProxy(in);
    QFontMetrics fm(font);
    resize(fm.horizontalAdvance(QLatin1Char('M')) * 80 + 40, fm.lineSpacing() * 25 + 60); // 80x25
    QObject::connect(in, &QLineEdit::returnPressed, this, [this]() {
        QByteArray line = in->text().toUtf8();
        in->clear();
        out->appendPlainText("> " + QString::fromUtf8(line)); // echo input, as the console does
        if (s_input)
            s_input(line.constData());
    });
}

// GetConsoleProcessList: Orbiter owns the console alone when it was not started from a terminal
bool ConsoleManager::IsConsoleExclusive(void) {
    return !isatty(STDIN_FILENO);
}

// GetConsoleWindow/ShowWindow: a launching terminal is not an Orbiter window; without one, Orbiter's own console
void ConsoleManager::ShowConsole(bool show)
{
    if (!IsConsoleExclusive())
        return;
    if (show) {
        if (!s_wnd)
            s_wnd = new ConsoleWnd;
        s_wnd->show();
        s_wnd->raise();
        s_wnd->activateWindow();
        s_wnd->setFocus();
    }
    else if (s_wnd)
        s_wnd->hide();
}

QWindow* ConsoleManager::ConsoleWindow(void)
{
    return (s_wnd ? s_wnd->windowHandle() : nullptr);
}

void ConsoleManager::SetConsoleTitle(const char* title)
{
    if (s_wnd)
        s_wnd->setWindowTitle(QString::fromUtf8(title));
}

bool ConsoleManager::WriteConsole(const char* text)
{
    ConsoleWnd* wnd = s_wnd;
    if (!wnd)
        return false;
    QString str = QString::fromUtf8(text);
    if (QThread::currentThread() == wnd->thread())
        wnd->out->appendPlainText(str);
    else // log output from worker threads
        QMetaObject::invokeMethod(wnd, [wnd, str]() { wnd->out->appendPlainText(str); }, Qt::QueuedConnection);
    return true;
}

void ConsoleManager::SetConsoleInput(void (*func)(const char* line))
{
    s_input = func;
}
