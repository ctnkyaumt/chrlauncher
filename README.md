<h1 align="center">
	<img src="/images/launchbro.ico"width="240" height="240" style="vertical-align: middle;">
	<span>&nbsp;
		launchbro
	</span>
</h1>

<p align="center">
	<a href="https://github.com/ctnkyaumt/launchbro/releases"><img src="https://img.shields.io/github/v/release/ctnkyaumt/launchbro?style=flat-square&include_prereleases&label=version" /></a>
	<a href="https://github.com/ctnkyaumt/launchbro/releases"><img src="https://img.shields.io/github/downloads/ctnkyaumt/launchbro/total.svg?style=flat-square" /></a>
	<a href="https://github.com/ctnkyaumt/launchbro/issues"><img src="https://img.shields.io/github/issues-raw/ctnkyaumt/launchbro.svg?style=flat-square&label=issues" /></a>
	<a href="https://github.com/ctnkyaumt/launchbro/graphs/contributors"><img src="https://img.shields.io/github/contributors/ctnkyaumt/launchbro?style=flat-square" /></a>
	<a href="https://github.com/ctnkyaumt/launchbro/blob/master/LICENSE"><img src="https://img.shields.io/github/license/ctnkyaumt/launchbro?style=flat-square" /></a>
</p>

-------

<p align="center">
	<img src="/images/launchbro.png?hgcv" />
</p>

### Description:
Small and very fast portable launcher and updater for Chromium.

### System requirements:
- Windows 7, 8, 8.1, 10, 11 32-bit/64-bit/ARM64
- An SSE2-capable CPU
- <s>KB2533623</s> KB3063858 update for Windows 7 was required [[x64](https://www.microsoft.com/en-us/download/details.aspx?id=47442) / [x32](https://www.microsoft.com/en-us/download/details.aspx?id=47409)]

### GPG Signature:
Binaries have GPG signature `launchbro.exe.sig` in application folder.

- Public key: [pubkey.asc](https://raw.githubusercontent.com/ctnkyaumt/builder/master/pubkey.asc) ([pgpkeys.eu](https://pgpkeys.eu/pks/lookup?op=index&fingerprint=on&search=0x5635B5FD))
- Key ID: 0x5635B5FD
- Fingerprint: D985 2361 1524 AB29 BE73 30AC 2881 20A7 5635 B5FD

### Default browser:
launchbro has feature to use portable Chromium as default browser and it will be open links from another programs through launchbro.
- start "SetDefaultBrowser.bat" (as admin).
- start "Control panel" -> "Default programs" -> "Set your default programs" -> "launchbro" and set all checkboxes on.

### Command line:
There is list of arguments overrides .ini options
~~~
-autodownload - auto download update and install it!
-bringtofront - bring launchbro window to front when download started
-forcecheck - force update checking
-wait - start browser only when check/download/install update complete
-update - use launchbro as updater, but does not start Chromium
~~~

### Supported browser:
- as launcher - Chromium based (like Google Chrome, Opera, Yandex Browser, Vivaldi, etc.) and Firefox based (Mozilla Firefox, r3dfox, Iceweasel, Basilisk, Pale Moon, Waterfox, etc.)
- as updater - Chromium, r3dfox, Iceweasel

### Chrome++:
- By default Chromium encrypt profile with user SID, which is disabled by [Chrome++](https://github.com/Bush2021/chrome_plus), so it is recomended. Starting with version 2.7 of launchbro it added support of Chrome++.

### Settings:
~~~ini
[launchbro]

# Custom Chromium update URL (string):
#ChromiumUpdateUrl=https://chromium.woolyss.com/api/v3/?os=windows&bit=%d&type=%s&out=string
#ChromiumUpdateUrl=https://github.com/uazo/cromite/releases/latest/download/updateurl.txt

# Command line for Chromium (string):
# See here: https://peter.sh/experiments/chromium-command-line-switches/
ChromiumCommandLine=--flag-switches-begin --user-data-dir=..\profile --no-default-browser-check --disable-logging --no-report-upload --flag-switches-end

# Command line for Firefox-based browsers (string):
# Used only when ChromiumType/ChromiumType2..4 is set to "r3dfox" or "iceweasel".
#FirefoxCommandLine=-no-remote
#FirefoxCommandLine2=-no-remote
#FirefoxCommandLine3=-no-remote
#FirefoxCommandLine4=-no-remote

# Chromium executable file name (string):
# If set, overrides auto-detection and ChromiumType-based executable selection.
#ChromiumBinary=chrome.exe

# Chromium binaries directory (string):
# Relative (to launchbro directory) or full path (env. variables supported).
ChromiumDirectory=.\bin

# Set Chromium binaries architecture (integer):
#
# 0	-> autodetect (default)
# 64	-> 64-bit
# 32	-> 32-bit
ChromiumArchitecture=0

# Auto download updates if found (boolean)
#
# false	-> show tray tip if update found, downloading manually (default)
# true	-> auto download update and install it!
ChromiumAutoDownload=false

# Bring launchbro window when download started (boolean)
#
# false	-> don't bring main window to front automatically
# true	-> bring launchbro window to front when download started (default)
ChromiumBringToFront=true

# Set download in foreground mode (boolean):
#
# false	-> start browser and check/download/install update in background
# true	-> start browser only when check/download/install update complete (default)
ChromiumWaitForDownloadEnd=true

# Use launchbro as updater, but does not start Chromium (boolean):
#
# false	-> update & start Chromium (default)
# true	-> download & install Chromium update without start
ChromiumUpdateOnly=true

# Type of Chromium builds:
#
# dev-official
#	Official development builds from snapshots repository
#	"storage.googleapis.com/chromium-browser-snapshots/index.html" (32/64 bit)
#
# stable-codecs-sync
#	Unofficial stable builds with codecs
#	"github.com/Hibbiki/chromium-win64/releases" (64 bit)
#	"github.com/Hibbiki/chromium-win32/releases" (32 bit)
#
# dev-nosync
#	Unofficial development builds without Google services
#	"github.com/RobRich999/Chromium_Clang/releases" (32/64 bit)
#
# dev-codecs-sync
#	Unofficial development builds with codecs and without Google services
#	"github.com/macchrome/winchrome/releases" (64 bit)
#
# dev-codecs-nosync
#	Unofficial development builds with codecs and without Google services
#	"github.com/macchrome/winchrome/releases" (64 bit)
#
# ungoogled-chromium
#	Unofficial builds without Google integration and enhanced privacy (based on Eloston project)
#	"github.com/macchrome/winchrome/releases/" (32/64 bit)
#	"github.com/Eloston/ungoogled-chromium"
#
# cromite
#	Unofficial builds based on Chromium with enhanced security and privacy
#	"github.com/uazo/cromite/releases"
#
# r3dfox / iceweasel
#	Firefox-based browsers supported as launcher + updater (portable releases via GitHub).
ChromiumType=ungoogled-chromium

# Type of Chromium builds for instances 2..4:
#ChromiumType2=ungoogled-chromium
#ChromiumType3=ungoogled-chromium
#ChromiumType4=ungoogled-chromium

# Check for new Chromium version once in X days (integer):
#
# 2	-> check updates once in a X days
# 0	-> disable update checking
# -1	-> force update checking
ChromiumCheckPeriod=0

# Last cached update checking timestamp (integer):
ChromiumLastCheck=0

# Start browser when downloading and installing is over (boolean)
#
ChromiumRunAtEnd=true

# Delete Chromium directory to recylcle when installing update (boolean)
#
ChromiumDeleteToRecycle=true

# A DLL hijack implements Chrome full portability as well as tab enhancements.
# https://github.com/Bush2021/chrome_plus
#ChromePlusDirectory=.\chrome_plus

#
# Internal settings (SDK)
#

# Set custom useragent (string):
#UserAgent=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36

# Set proxy configuration (string):
#Proxy=127.0.0.1:80
~~~
