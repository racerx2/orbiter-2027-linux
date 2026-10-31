Wine bridge for Orbiter 2027 Linux
==================================

Windows add-ons (.dll modules: MFDs, plugins, vessels) in the Linux Orbiter.
Each module runs in a small Windows helper under Wine, and Orbiter talks to it.

Install
-------
- The bridge comes with Orbiter: Modules/WineBridge.so and Modules/WineBridge/.
- Install Windows add-ons as on Windows: unzip them into the Orbiter folder.
  Activate a plugin in the Launchpad's Modules tab as usual.

Wine
----
The helper is a 32-bit Windows program, so Wine has to be able to run 32-bit
programs.
- Linux Mint: Software Manager, "Wine" (or: sudo apt install wine-installer)
- Ubuntu, Debian: sudo dpkg --add-architecture i386 && sudo apt update
                  sudo apt install wine wine32:i386
- Other distributions: your distribution's Wine with 32-bit support.
Tested on Linux Mint 22.3 with the Software Manager's Wine (9.0) and with
WineHQ's stable Wine (11).

First start
-----------
The first time a Windows module loads, Orbiter makes a Wine prefix in the
Orbiter folder (WineBridge/prefix), which takes a few seconds. Later starts
take about half a second.

When a module doesn't load
--------------------------
Orbiter.log has the bridge's lines (they start with "WineBridge:"). Wine's own
output is in WineBridge.log in the Orbiter folder.

Notices
-------
- lua.dll, lua5.1.dll: Lua 5.1, Copyright (C) 1994-2006 Lua.org, PUC-Rio.
  MIT license (below).
- LuaInterpreter.dll, LuaInline.dll, and the Orbiter API in Orbiter.exe: built
  from Orbiter's source and SDK, Copyright (c) 2000-2026 Martin Schweiger.
  MIT license (below).
- Orbiter.exe contains zlib 1.2.11, Copyright (C) 1995-2017 Jean-loup Gailly
  and Mark Adler, under the zlib license (below).
- The Windows files link Wine's start-up and runtime code (winecrt0,
  compiler-rt, libc++ as Wine ships them), under their licenses: Wine's LGPL
  2.1 or later (https://www.winehq.org/license), LLVM's Apache 2.0 with LLVM
  exceptions.

MIT license:
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

zlib license:
This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the
use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim
   that you wrote the original software. If you use this software in a
   product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
