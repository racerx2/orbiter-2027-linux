// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#include "Help.h"
#include "ChmHelp.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

void OpenHelp (QWidget *hWnd, const char *file, const char *topic)
{
	char topic_file[256];
	if (strlen(topic) < 4 || strncasecmp(topic + strlen(topic) - 4, ".htm", 4))
		snprintf(topic_file, 256, "%s.htm", topic);
	else
		snprintf(topic_file, 256, "%s", topic);
	HtmlHelp (hWnd, file, topic_file);
}

void OpenDefaultHelp (QWidget *hWnd, const char *topic)
{
	OpenHelp (hWnd, "html\\orbiter.chm", topic);
}
