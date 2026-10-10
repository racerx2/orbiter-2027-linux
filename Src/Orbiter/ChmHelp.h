// not upstream: HtmlHelp API and .chm reader; a .chm here is a zip of the project's pages (cmake/hhc.py)

#ifndef __CHMHELP_H
#define __CHMHELP_H

#include <QTextBrowser>
#include <QUrl>
#include <utility>
#include <vector>

class QWidget;
class QWindow;

// HH_DISPLAY_TOPIC counterpart: opens the help window on a topic of a help file ("file.chm" or "file.chm::/topic.htm");
// topic may be NULL for the project's default topic. Returns false if the help file is not found.
bool HtmlHelp (QWidget *owner, const char *file, const char *topic);
bool HtmlHelp (QWindow *owner, const char *file, const char *topic); // hwndCaller a QWindow (the render window): the help stays above it and closes with it

// page inside a help file as a URL the ChmBrowser loads: chm:<absolute .chm path>/<topic>
QUrl ChmUrl (const QString &chmfile, const QString &topic);

// "its:" / "ms-its:" / "mk:@MSITStore:" URLs ("its:Html\\Scenarios\\x.chm::/y.htm") -> ChmUrl, invalid if not a help page
QUrl ChmUrlFromIts (const QString &its);

// page HTML and style sheets without their own colours, so the palette (the desktop theme) shows them
QString ThemeHtml (const QString &html);
QString ThemeCss (const QString &css);

// text browser that also shows the pages of help files, in the desktop theme
class ChmBrowser: public QTextBrowser {
public:
	explicit ChmBrowser (QWidget *parent = nullptr);
	QVariant loadResource (int type, const QUrl &name) override;
	void SetPageHtml (const QString &html); // setHtml, with percentage image widths as the browser object sized them

protected:
	void doSetSource (const QUrl &name, QTextDocument::ResourceType type) override;
	void resizeEvent (QResizeEvent *e) override;
	void changeEvent (QEvent *e) override; // palette or style change: the page again in the new colours

private:
	QVariant LoadPage (int type, const QUrl &name);
	void ThemeStyle (); // the document's default style sheet from the palette
	QString pageHtml;   // the last SetPageHtml page, for a re-render
	bool rerender = false;
	int PageWidth () const;
	QString PercentImages (const QString &html);
	void FitPercentImages ();
	std::vector<std::pair<QString,double>> pctImages; // <img width="N%"> of the page in order: src, N
	int pctWidth = 0;                                // page width they were fitted to
};

#endif // !__CHMHELP_H
