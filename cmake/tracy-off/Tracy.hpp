// not upstream: Tracy's macros as no-ops, for builds without the profiler (tracy.cmake fetches Tracy only when it is on)
#pragma once
#define ZoneScoped
#define ZoneScopedN(name)
#define ZoneScopedC(color)
#define ZoneScopedNC(name, color)
#define ZoneText(text, size)
#define ZoneName(text, size)
#define FrameMark
#define FrameMarkNamed(name)
#define FrameMarkStart(name)
#define FrameMarkEnd(name)
#define TracyPlot(name, val)
#define TracyMessage(txt, size)
#define TracyMessageL(txt)
