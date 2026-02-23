// chrlauncher
// Copyright (c) 2015-2025 Henry++

#include "routine.h"

#include "main.h"
#include "rapp.h"

VOID _app_update_browser_info (
	_In_ HWND hwnd,
	_In_ PBROWSER_INFORMATION pbi
)
{
	PR_STRING localized_string;
	PR_STRING date_dormat;
	LPWSTR string;
	R_STRINGREF empty_string;
	HDWP hdefer;

	string = _r_locale_getstring (IDS_STATUS_NOTFOUND);

	_r_obj_initializestringref (&empty_string, string);

	date_dormat = _r_format_unixtime (pbi->timestamp, FDTF_SHORTDATE | FDTF_SHORTTIME);

	localized_string = _r_format_string (L"%s:", _r_locale_getstring (IDS_BROWSER));

	hdefer = BeginDeferWindowPos (2);

	_r_ctrl_settablestring (
		hwnd,
		&hdefer,
		IDC_BROWSER,
		&localized_string->sr,
		IDC_BROWSER_DATA,
		pbi->browser_name ? &pbi->browser_name->sr : &empty_string
	);

	_r_obj_movereference ((PVOID_PTR)&localized_string, _r_format_string (L"%s:", _r_locale_getstring (IDS_CURRENTVERSION)));

	_r_ctrl_settablestring (
		hwnd,
		&hdefer,
		IDC_CURRENTVERSION,
		&localized_string->sr,
		IDC_CURRENTVERSION_DATA,
		pbi->current_version ? &pbi->current_version->sr : &empty_string
	);

	_r_obj_movereference ((PVOID_PTR)&localized_string, _r_format_string (L"%s:", _r_locale_getstring (IDS_VERSION)));

	_r_ctrl_settablestring (
		hwnd,
		&hdefer,
		IDC_VERSION,
		&localized_string->sr,
		IDC_VERSION_DATA,
		pbi->new_version ? &pbi->new_version->sr : &empty_string
	);

	_r_obj_movereference ((PVOID_PTR)&localized_string, _r_format_string (L"%s:", _r_locale_getstring (IDS_DATE)));

	_r_ctrl_settablestring (
		hwnd,
		&hdefer,
		IDC_DATE,
		&localized_string->sr,
		IDC_DATE_DATA,
		date_dormat ? &date_dormat->sr : &empty_string
	);

	if (hdefer)
		EndDeferWindowPos (hdefer);

	if (date_dormat)
		_r_obj_dereference (date_dormat);

	_r_obj_dereference (localized_string);
}

VOID _app_setstatus (
	_In_ HWND hwnd,
	_In_opt_ HWND htaskbar,
	_In_opt_ LPCWSTR string,
	_In_opt_ ULONG64 total_read,
	_In_opt_ ULONG64 total_length
)
{
	LONG64 percent = 0;

	if (htaskbar)
	{
		_r_taskbar_setprogressstate (htaskbar, hwnd, TBPF_NORMAL);

		_r_taskbar_setprogressvalue (htaskbar, hwnd, total_read, total_length);
	}

	if (!total_read && total_length)
	{
		_r_status_settextformat (hwnd, IDC_STATUSBAR, 0, L"%s 0%%", string);

		if (!_r_str_isempty (string))
		{
			_r_tray_setinfoformat (hwnd, &GUID_TrayIcon, NULL, L"%s\r\n%s: 0%%", _r_app_getname (), string);
		}
		else
		{
			_r_tray_setinfo (hwnd, &GUID_TrayIcon, NULL, _r_app_getname ());
		}
	}
	else if (total_read && total_length)
	{
		percent = _r_calc_clamp64 ((ULONG64)PR_CALC_PERCENTOF (total_read, total_length), 0, 100);

		_r_status_settextformat (hwnd, IDC_STATUSBAR, 0, L"%s %" TEXT (PR_LONG64) L"%%", string, percent);

		if (!_r_str_isempty (string))
		{
			_r_tray_setinfoformat (hwnd, &GUID_TrayIcon, NULL, L"%s\r\n%s: %" TEXT (PR_LONG64) L"%%", _r_app_getname (), string, percent);
		}
		else
		{
			_r_tray_setinfo (hwnd, &GUID_TrayIcon, NULL, _r_app_getname ());
		}
	}
	else
	{
		_r_status_settext (hwnd, IDC_STATUSBAR, 0, string);

		if (!_r_str_isempty (string))
		{
			_r_tray_setinfoformat (hwnd, &GUID_TrayIcon, NULL, L"%s\r\n%s", _r_app_getname (), string);
		}
		else
		{
			_r_tray_setinfo (hwnd, &GUID_TrayIcon, NULL, _r_app_getname ());
		}
	}

	_r_wnd_sendmessage (hwnd, IDC_PROGRESS, PBM_SETPOS, (WPARAM)(LONG)percent, 0);
}
