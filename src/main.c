// launchbro
// 2026 ctnkyaumt

#include "routine.h"

#include "main.h"
#include "rapp.h"

#include "resource.h"
#include <shellapi.h>
#include <tlhelp32.h>

BROWSER_INFORMATION browser_info = {0};

R_QUEUED_LOCK lock_download = PR_QUEUED_LOCK_INIT;
R_QUEUED_LOCK lock_thread = PR_QUEUED_LOCK_INIT;

R_WORKQUEUE workqueue;

typedef enum _APP_ICON_STATE
{
	APP_ICON_STATE_IDLE = 0,
	APP_ICON_STATE_CHROMIUM,
	APP_ICON_STATE_FIREFOX
} APP_ICON_STATE;

HICON hicon_app_small = NULL;
HICON hicon_app_large = NULL;
APP_ICON_STATE current_icon_state = APP_ICON_STATE_IDLE;

extern BOOLEAN _app_is_firefox_fork (PBROWSER_INFORMATION pbi);

static INT _app_geticonresourceid (
	_In_ APP_ICON_STATE icon_state
)
{
	switch (icon_state)
	{
		case APP_ICON_STATE_CHROMIUM:
			return IDI_CHROMIUM;

		case APP_ICON_STATE_FIREFOX:
			return IDI_FIREFOX;

		default:
			return IDI_MAIN;
	}
}

VOID _app_seticonstate (
	_In_ HWND hwnd,
	_In_opt_ PBROWSER_INFORMATION pbi,
	_In_ BOOLEAN is_busy,
	_In_ BOOLEAN is_forced
)
{
	APP_ICON_STATE icon_state;
	INT icon_resource;
	LONG dpi_value;
	LONG icon_small;
	LONG icon_large;

	if (is_busy && pbi)
	{
		icon_state = _app_is_firefox_fork (pbi) ? APP_ICON_STATE_FIREFOX : APP_ICON_STATE_CHROMIUM;
	}
	else
	{
		icon_state = APP_ICON_STATE_IDLE;
	}

	if (!is_forced && current_icon_state == icon_state && hicon_app_small && hicon_app_large)
		return;

	current_icon_state = icon_state;
	icon_resource = _app_geticonresourceid (icon_state);
	dpi_value = _r_dc_gettaskbardpi ();
	icon_small = _r_dc_getsystemmetrics (SM_CXSMICON, dpi_value);
	icon_large = _r_dc_getsystemmetrics (SM_CXICON, dpi_value);

	if (hicon_app_small)
	{
		DestroyIcon (hicon_app_small);
		hicon_app_small = NULL;
	}

	if (hicon_app_large)
	{
		DestroyIcon (hicon_app_large);
		hicon_app_large = NULL;
	}

	hicon_app_small = (HICON)LoadImageW (_r_sys_getimagebase (), MAKEINTRESOURCEW (icon_resource), IMAGE_ICON, icon_small, icon_small, LR_DEFAULTCOLOR);
	hicon_app_large = (HICON)LoadImageW (_r_sys_getimagebase (), MAKEINTRESOURCEW (icon_resource), IMAGE_ICON, icon_large, icon_large, LR_DEFAULTCOLOR);

	if (!hicon_app_small)
		hicon_app_small = CopyIcon (_r_sys_loadsharedicon (_r_sys_getimagebase (), MAKEINTRESOURCE (icon_resource), icon_small));

	if (!hicon_app_large)
		hicon_app_large = CopyIcon (_r_sys_loadsharedicon (_r_sys_getimagebase (), MAKEINTRESOURCE (icon_resource), icon_large));

	if (hicon_app_small)
		SendMessageW (hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hicon_app_small);

	if (hicon_app_large)
		SendMessageW (hwnd, WM_SETICON, ICON_BIG, (LPARAM)hicon_app_large);

	if (hicon_app_small)
		_r_tray_setinfo (hwnd, &GUID_TrayIcon, hicon_app_small, _r_app_getname ());
}

VOID _app_set_lastcheck (
	_In_ PBROWSER_INFORMATION pbi
);

VOID _app_create_profileshortcut (
	_In_ PBROWSER_INFORMATION pbi
);

VOID _app_parse_args (
	_Inout_ PBROWSER_INFORMATION pbi
);

VOID _app_openbrowser (
	_In_ PBROWSER_INFORMATION pbi
);

VOID _app_taskupdate_closebrowser (
	_In_ PBROWSER_INFORMATION pbi,
	_Out_ PBOOLEAN was_running_ptr
);

VOID _app_update_browser_info (
	_In_ HWND hwnd,
	_In_ PBROWSER_INFORMATION pbi
);

VOID _app_setstatus (
	_In_ HWND hwnd,
	_In_opt_ HWND htaskbar,
	_In_opt_ PBROWSER_INFORMATION pbi,
	_In_opt_ LPCWSTR string,
	_In_opt_ ULONG64 total_read,
	_In_opt_ ULONG64 total_length
);

BOOLEAN _app_ishaveupdate (
	_In_ PBROWSER_INFORMATION pbi
);

BOOLEAN _app_isupdatedownloaded (
	_In_ PBROWSER_INFORMATION pbi
);

BOOLEAN _app_isupdaterequired (
	_In_ PBROWSER_INFORMATION pbi
);

ULONG _app_getactionid (
	_In_ PBROWSER_INFORMATION pbi
);

VOID _app_init_browser_info (
	_Inout_ PBROWSER_INFORMATION pbi
);

BOOLEAN _app_checkupdate (
	_In_ HWND hwnd,
	_In_ PBROWSER_INFORMATION pbi,
	_Out_ PBOOLEAN is_error_ptr
);

BOOLEAN _app_downloadupdate (
	_In_ HWND hwnd,
	_In_ PBROWSER_INFORMATION pbi,
	_Out_ PBOOLEAN is_error_ptr
);

BOOLEAN _app_installupdate (
	_In_ HWND hwnd,
	_In_ PBROWSER_INFORMATION pbi,
	_Out_ PBOOLEAN is_error_ptr
)
;

BOOLEAN _app_is_instance_configured (
	_In_ LONG instance_id
);

VOID _app_clear_browser_info_references (
	_Inout_ PBROWSER_INFORMATION pbi
);

VOID _app_update_secondary_instance (
	_In_ HWND hwnd,
	_Inout_ PBROWSER_INFORMATION pbi
);

BOOLEAN _app_patch_registry_profile (
	_In_ PBROWSER_INFORMATION pbi
);

BOOLEAN _app_is_registry_patched (
	_In_ PBROWSER_INFORMATION pbi
);

BOOLEAN _app_ensure_registry_profile (
	_In_ HWND hwnd,
	_In_ PBROWSER_INFORMATION pbi
);

VOID _app_thread_taskupdate_all (
	_In_ HWND hwnd,
	_Inout_ PBROWSER_INFORMATION primary
);

VOID _app_thread_check (
	_In_ PVOID arglist
)
{
	PBROWSER_INFORMATION pbi;
	HWND hwnd;
	ULONG locale_id;
	BOOLEAN is_haveerror = FALSE;
	BOOLEAN is_stayopen = FALSE;
	BOOLEAN is_installed = FALSE;
	BOOLEAN is_updaterequired;
	BOOLEAN is_exists;

	pbi = (PBROWSER_INFORMATION)arglist;
	hwnd = _r_app_gethwnd ();

	_r_queuedlock_acquireshared (&lock_thread);

	_r_ctrl_enable (hwnd, IDC_START_BTN, FALSE);

	_r_progress_setmarquee (hwnd, IDC_PROGRESS, TRUE);

	locale_id = _app_getactionid (pbi);

	_r_ctrl_setstring (hwnd, IDC_START_BTN, _r_locale_getstring (locale_id));

	if (pbi->is_taskupdate)
	{
		_app_thread_taskupdate_all (hwnd, pbi);
		return;
	}

	{
		LONG requested_order = (pbi->instance_id >= 1 && pbi->instance_id <= 4) ? pbi->instance_id : 0;
		LONG last_pre = requested_order ? (requested_order - 1) : 4;

		for (LONG instance_id = 1; instance_id <= last_pre; instance_id++)
		{
			BROWSER_INFORMATION instance_info = {0};

			if (!_app_is_instance_configured (instance_id))
				continue;

			instance_info.hwnd = pbi->hwnd;
			instance_info.htaskbar = pbi->htaskbar;
			instance_info.instance_id = instance_id;
			instance_info.architecture = 0;
			instance_info.is_forcecheck = pbi->is_forcecheck;
			instance_info.is_autodownload = pbi->is_autodownload;

			_app_init_browser_info (&instance_info);
			_app_update_secondary_instance (hwnd, &instance_info);
			_app_clear_browser_info_references (&instance_info);
		}
	}

	// unpack downloaded package
	if (_app_isupdatedownloaded (pbi))
	{
		_r_progress_setmarquee (hwnd, IDC_PROGRESS, FALSE);

		_r_tray_toggle (hwnd, &GUID_TrayIcon, TRUE); // show tray icon

		if (!_r_fs_isfileused (&pbi->binary_path->sr))
		{
			if (pbi->is_bringtofront)
				_r_wnd_toggle (hwnd, TRUE); // show window

			if (_app_installupdate (hwnd, pbi, &is_haveerror))
			{
				_app_init_browser_info (pbi);

				_app_update_browser_info (hwnd, pbi);

				_app_set_lastcheck (pbi);

				_app_create_profileshortcut (pbi);
				is_installed = TRUE;
			}
		}
		else
		{
			_r_ctrl_enable (hwnd, IDC_START_BTN, TRUE);

			is_stayopen = TRUE;
		}
	}

	// check/download/unpack
	if (!is_installed && !is_stayopen)
	{
		_r_progress_setmarquee (hwnd, IDC_PROGRESS, TRUE);

		is_updaterequired = _app_isupdaterequired (pbi);
		is_exists = _r_fs_exists (&pbi->binary_path->sr);

		// show launcher gui
		if (!is_exists || is_updaterequired || pbi->is_onlyupdate || pbi->is_bringtofront)
		{
			_r_tray_toggle (hwnd, &GUID_TrayIcon, TRUE); // show tray icon

			_r_wnd_toggle (hwnd, TRUE);
		}

		if (is_exists)
		{
			if (_r_config_getboolean (L"ChromiumRunAtEnd", TRUE))
			{
				if (!pbi->is_waitdownloadend && !pbi->is_onlyupdate)
					_app_openbrowser (pbi);
			}
		}

		if (_app_checkupdate (hwnd, pbi, &is_haveerror))
		{
			_r_tray_toggle (hwnd, &GUID_TrayIcon, TRUE); // show tray icon

			if ((!is_exists || pbi->is_autodownload) && _app_ishaveupdate (pbi))
			{
				if (pbi->is_bringtofront)
					_r_wnd_toggle (hwnd, TRUE); // show window

				if (_r_config_getboolean (L"ChromiumRunAtEnd", TRUE))
				{
					if (is_exists && !pbi->is_onlyupdate && !pbi->is_waitdownloadend && !_app_isupdatedownloaded (pbi))
						_app_openbrowser (pbi);
				}

				_r_progress_setmarquee (hwnd, IDC_PROGRESS, FALSE);

				if (_app_downloadupdate (hwnd, pbi, &is_haveerror))
				{
					if (!_r_fs_isfileused (&pbi->binary_path->sr))
					{
						_r_ctrl_enable (hwnd, IDC_START_BTN, FALSE);

						if (_app_installupdate (hwnd, pbi, &is_haveerror))
						{
							_app_set_lastcheck (pbi);
							_app_create_profileshortcut (pbi);
						}
					}
					else
					{
						_r_tray_popup (hwnd, &GUID_TrayIcon, NIIF_INFO, _r_app_getname (), _r_locale_getstring (IDS_STATUS_DOWNLOADED)); // inform user

						locale_id = _app_getactionid (pbi);

						_r_ctrl_setstring (hwnd, IDC_START_BTN, _r_locale_getstring (locale_id));

						_r_ctrl_enable (hwnd, IDC_START_BTN, TRUE);

						is_stayopen = TRUE;
					}
				}
				else
				{
					_r_tray_popupformat (hwnd, &GUID_TrayIcon, NIIF_INFO, _r_app_getname (), _r_locale_getstring (IDS_STATUS_FOUND), pbi->new_version->buffer); // just inform user

					locale_id = _app_getactionid (pbi);

					_r_ctrl_setstring (hwnd, IDC_START_BTN, _r_locale_getstring (locale_id));

					_r_ctrl_enable (hwnd, IDC_START_BTN, TRUE);

					is_stayopen = TRUE;
				}
			}

			if (!pbi->is_autodownload && !_app_isupdatedownloaded (pbi))
			{
				_r_tray_popupformat (hwnd, &GUID_TrayIcon, NIIF_INFO, _r_app_getname (), _r_locale_getstring (IDS_STATUS_FOUND), pbi->new_version->buffer); // just inform user

				locale_id = _app_getactionid (pbi);

				_r_ctrl_setstring (hwnd, IDC_START_BTN, _r_locale_getstring (locale_id));

				_r_ctrl_enable (hwnd, IDC_START_BTN, TRUE);

				is_stayopen = TRUE;
			}
		}
	}

	{
		LONG requested_order = (pbi->instance_id >= 1 && pbi->instance_id <= 4) ? pbi->instance_id : 0;

		if (requested_order >= 1 && requested_order < 4)
		{
			for (LONG instance_id = requested_order + 1; instance_id <= 4; instance_id++)
			{
				BROWSER_INFORMATION instance_info = {0};

				if (!_app_is_instance_configured (instance_id))
					continue;

				instance_info.hwnd = pbi->hwnd;
				instance_info.htaskbar = pbi->htaskbar;
				instance_info.instance_id = instance_id;
				instance_info.architecture = 0;
				instance_info.is_forcecheck = pbi->is_forcecheck;
				instance_info.is_autodownload = pbi->is_autodownload;

				_app_init_browser_info (&instance_info);
				_app_update_secondary_instance (hwnd, &instance_info);
				_app_clear_browser_info_references (&instance_info);
			}
		}
	}

	_r_progress_setmarquee (hwnd, IDC_PROGRESS, FALSE);

	if (is_haveerror || pbi->is_onlyupdate)
	{
		locale_id = _app_getactionid (pbi);

		_r_ctrl_setstring (hwnd, IDC_START_BTN, _r_locale_getstring (locale_id));

		_r_ctrl_enable (hwnd, IDC_START_BTN, TRUE);

		if (is_haveerror)
		{
			_r_tray_popup (hwnd, &GUID_TrayIcon, NIIF_ERROR, _r_app_getname (), _r_locale_getstring (IDS_STATUS_ERROR)); // just inform user

			_app_setstatus (hwnd, pbi->htaskbar, pbi, _r_locale_getstring (IDS_STATUS_ERROR), 0, 0);
		}

		is_stayopen = TRUE;
	}

	if (_r_fs_exists (&pbi->binary_path->sr))
	{
		_app_create_profileshortcut (pbi);

		if (_r_config_getboolean (L"PatchRegistryProfile", TRUE))
			_app_ensure_registry_profile (hwnd, pbi);
	}

	if (_r_config_getboolean (L"ChromiumRunAtEnd", TRUE) && !pbi->is_onlyupdate)
		_app_openbrowser (pbi);

	_r_queuedlock_releaseshared (&lock_thread);

	if (is_stayopen)
	{
		_app_update_browser_info (hwnd, pbi);
	}
	else
	{
		_r_wnd_sendmessage (hwnd, 0, WM_DESTROY, 0, 0);
	}
}

BOOLEAN _app_ensure_registry_profile (
	_In_ HWND hwnd,
	_In_ PBROWSER_INFORMATION pbi
)
{
	PR_STRING cmdline;
	NTSTATUS status;

	if (!pbi || _r_obj_isstringempty (pbi->binary_path) || _r_obj_isstringempty (pbi->profile_dir))
		return FALSE;

	if (!_r_fs_exists (&pbi->binary_path->sr))
		return FALSE;

	// already patched, nothing to do
	if (_app_is_registry_patched (pbi))
		return TRUE;

	// registry keys don't exist yet - browser needs to run once to register itself
	// prompt user before doing this
	if (_r_show_message (hwnd, MB_YESNO | MB_ICONQUESTION, NULL,
		L"Chromium needs to run once to register as a browser before the profile path can be set in the registry.\n\n"
		L"This will launch Chromium briefly and close it automatically.\n\n"
		L"Do you want to proceed?") != IDYES)
		return FALSE;

	// launch browser with --no-first-run --user-data-dir to create profile and register
	cmdline = _r_format_string (L"\"%s\" --no-first-run --user-data-dir=\"%s\"",
		pbi->binary_path->buffer,
		pbi->profile_dir->buffer);

	if (!cmdline)
		return FALSE;

	status = _r_sys_createprocess (pbi->binary_path->buffer, cmdline->buffer, pbi->binary_dir->buffer, FALSE);

	_r_obj_dereference (cmdline);

	if (!NT_SUCCESS (status))
		return FALSE;

	// wait a few seconds for the browser to register itself and create profile files
	Sleep (5000);

	// try to find and close the browser process
	{
		HANDLE snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);

		if (snapshot != INVALID_HANDLE_VALUE)
		{
			PROCESSENTRY32W pe = {0};
			pe.dwSize = sizeof (pe);

			if (Process32FirstW (snapshot, &pe))
			{
				R_STRINGREF binary_name_sr;
				R_STRINGREF exe_name_sr;

				_r_path_getpathinfo (&pbi->binary_path->sr, NULL, &binary_name_sr);

				do
				{
					_r_obj_initializestringref (&exe_name_sr, pe.szExeFile);

					if (_r_str_isequal (&binary_name_sr, &exe_name_sr, TRUE))
					{
					HANDLE hproc = OpenProcess (PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);

						if (hproc)
						{
							TerminateProcess (hproc, 0);

							// wait for process to exit
							WaitForSingleObject (hproc, 10000);

							NtClose (hproc);
						}

						break;
					}
				}
				while (Process32NextW (snapshot, &pe));
			}

			NtClose (snapshot);
		}
	}

	// give it a moment after closing
	Sleep (2000);

	// now try to patch the registry
	return _app_patch_registry_profile (pbi);
}

INT_PTR CALLBACK DlgProc (
	_In_ HWND hwnd,
	_In_ UINT msg,
	_In_ WPARAM wparam,
	_In_ LPARAM lparam
)
{
	switch (msg)
	{
		case WM_INITDIALOG:
		{
			HWND htip;

			htip = _r_ctrl_createtip (hwnd);

			if (!htip)
				break;

			_r_ctrl_settiptext (htip, hwnd, IDC_BROWSER_DATA, LPSTR_TEXTCALLBACK);
			_r_ctrl_settiptext (htip, hwnd, IDC_CURRENTVERSION_DATA, LPSTR_TEXTCALLBACK);
			_r_ctrl_settiptext (htip, hwnd, IDC_VERSION_DATA, LPSTR_TEXTCALLBACK);
			_r_ctrl_settiptext (htip, hwnd, IDC_DATE_DATA, LPSTR_TEXTCALLBACK);

			break;
		}

		case RM_INITIALIZE:
		{
			HMENU hmenu;
			HICON hicon;
			LONG icon_small;
			LONG dpi_value;
			BOOLEAN is_hidden;
			BOOLEAN is_taskenabled;

			dpi_value = _r_dc_gettaskbardpi ();

			icon_small = _r_dc_getsystemmetrics (SM_CXSMICON, dpi_value);

			browser_info.hwnd = hwnd;

			_app_init_browser_info (&browser_info);

			_app_seticonstate (hwnd, &browser_info, FALSE, TRUE);

			hicon = hicon_app_small ? hicon_app_small : _r_sys_loadsharedicon (_r_sys_getimagebase (), MAKEINTRESOURCE (IDI_MAIN), icon_small);

			is_hidden = (_r_queuedlock_islocked (&lock_download) || _app_isupdatedownloaded (&browser_info)) ? FALSE : TRUE;

			_r_tray_create (hwnd, &GUID_TrayIcon, RM_TRAYICON, hicon, _r_app_getname (), is_hidden);

			hmenu = GetMenu (hwnd);

			if (hmenu)
			{
				is_taskenabled = _r_config_getboolean (L"TaskUpdateEnabled", FALSE);

				if (is_taskenabled && !_app_taskupdate_istaskpresent ())
				{
					is_taskenabled = FALSE;
					_r_config_setboolean (L"TaskUpdateEnabled", FALSE);
				}
				else if (is_taskenabled)
				{
					_app_taskupdate_setstartwhenavailable ();
				}

				_r_menu_checkitem (hmenu, IDM_RUNATEND_CHK, 0, MF_BYCOMMAND, _r_config_getboolean (L"ChromiumRunAtEnd", TRUE));
				_r_menu_checkitem (hmenu, IDM_DARKMODE_CHK, 0, MF_BYCOMMAND, _r_theme_isenabled ());
				_r_menu_checkitem (hmenu, IDM_TASKUPDATE_CHK, 0, MF_BYCOMMAND, is_taskenabled);
				_r_menu_checkitem (hmenu, IDM_AUTOCHECKUPDATES_CHK, 0, MF_BYCOMMAND, _r_config_getboolean (L"AutoCheckUpdates", TRUE));
			}

			_r_taskbar_initialize (&browser_info.htaskbar);

			_r_workqueue_queueitem (&workqueue, &_app_thread_check, &browser_info);

			break;
		}

		case RM_UNINITIALIZE:
		{
			_r_tray_destroy (hwnd, &GUID_TrayIcon);

			if (browser_info.htaskbar)
				_r_taskbar_destroy (&browser_info.htaskbar);

			break;
		}

		case RM_LOCALIZE:
		{
			// localize menu
			HMENU hmenu;
			HMENU hsubmenu;
			ULONG locale_id;

			hmenu = GetMenu (hwnd);

			if (hmenu)
			{
				_r_menu_setitemtext (hmenu, 0, TRUE, _r_locale_getstring (IDS_FILE));
				_r_menu_setitemtext (hmenu, 1, TRUE, _r_locale_getstring (IDS_SETTINGS));
				_r_menu_setitemtext (hmenu, 2, TRUE, _r_locale_getstring (IDS_HELP));

				hsubmenu = GetSubMenu (hmenu, 1);

				if (hsubmenu)
					_r_menu_setitemtextformat (hsubmenu, LANG_MENU, TRUE, L"%s (Language)", _r_locale_getstring (IDS_LANGUAGE));

				_r_menu_setitemtextformat (hmenu, IDM_RUN, FALSE, L"%s...", _r_locale_getstring (IDS_RUN));
				_r_menu_setitemtextformat (hmenu, IDM_OPEN, FALSE, L"%s...", _r_locale_getstring (IDS_OPEN));
				_r_menu_setitemtext (hmenu, IDM_EXIT, FALSE, _r_locale_getstring (IDS_EXIT));
				_r_menu_setitemtext (hmenu, IDM_RUNATEND_CHK, FALSE, _r_locale_getstring (IDS_RUNATEND_CHK));
				_r_menu_setitemtext (hmenu, IDM_DARKMODE_CHK, FALSE, _r_locale_getstring (IDS_DARKMODE_CHK));
				_r_menu_setitemtext (hmenu, IDM_TASKUPDATE_CHK, FALSE, _r_locale_getstring (IDS_TASKUPDATE_CHK));
				_r_menu_setitemtext (hmenu, IDM_AUTOCHECKUPDATES_CHK, FALSE, _r_locale_getstring (IDS_AUTOCHECKUPDATES_CHK));
				_r_menu_setitemtext (hmenu, IDM_CHECKFORUPDATES, FALSE, _r_locale_getstring (IDS_CHECKFORUPDATES));
				_r_menu_setitemtext (hmenu, IDM_EXPORTPROFILE, FALSE, _r_locale_getstring (IDS_EXPORTPROFILE));
				_r_menu_setitemtext (hmenu, IDM_IMPORTPROFILE, FALSE, _r_locale_getstring (IDS_IMPORTPROFILE));
				_r_menu_setitemtext (hmenu, IDM_UNINSTALL, FALSE, _r_locale_getstring (IDS_UNINSTALL));
				_r_menu_setitemtext (hmenu, IDM_WEBSITE, FALSE, _r_locale_getstring (IDS_WEBSITE));
				_r_menu_setitemtextformat (hmenu, IDM_ABOUT, FALSE, L"%s\tF1", _r_locale_getstring (IDS_ABOUT));

				// enum localizations
				_r_locale_enum ((HWND)GetSubMenu (hmenu, 1), LANG_MENU, IDX_LANGUAGE);
			}

			_app_update_browser_info (hwnd, &browser_info);

			_r_ctrl_setstring (hwnd, IDC_LINKS, FOOTER_STRING);

			locale_id = _app_getactionid (&browser_info);

			_r_ctrl_setstring (hwnd, IDC_START_BTN, _r_locale_getstring (locale_id));

			break;
		}

		case RM_TASKBARCREATED:
		{
			HICON hicon;
			LONG dpi_value;
			LONG icon_small;
			BOOLEAN is_hidden;

			dpi_value = _r_dc_gettaskbardpi ();

			icon_small = _r_dc_getsystemmetrics (SM_CXSMICON, dpi_value);

			_app_seticonstate (hwnd, &browser_info, FALSE, TRUE);

			hicon = hicon_app_small ? hicon_app_small : _r_sys_loadsharedicon (_r_sys_getimagebase (), MAKEINTRESOURCE (IDI_MAIN), icon_small);

			is_hidden = (_r_queuedlock_islocked (&lock_download) || _app_isupdatedownloaded (&browser_info)) ? FALSE : TRUE;

			_r_tray_create (hwnd, &GUID_TrayIcon, RM_TRAYICON, hicon, _r_app_getname (), is_hidden);

			break;
		}

		case WM_DPICHANGED:
		{
			HICON hicon;
			LONG dpi_value;
			LONG icon_small;

			dpi_value = _r_dc_gettaskbardpi ();

			icon_small = _r_dc_getsystemmetrics (SM_CXSMICON, dpi_value);

			_app_seticonstate (hwnd, &browser_info, FALSE, TRUE);

			hicon = hicon_app_small ? hicon_app_small : _r_sys_loadsharedicon (_r_sys_getimagebase (), MAKEINTRESOURCE (IDI_MAIN), icon_small);

			_r_tray_setinfo (hwnd, &GUID_TrayIcon, hicon, _r_app_getname ());

			_r_wnd_sendmessage (hwnd, IDC_STATUSBAR, WM_SIZE, 0, 0);

			break;
		}

		case WM_CLOSE:
		{
			if (_r_queuedlock_islocked (&lock_download))
			{
				if (_r_show_message (hwnd, MB_YESNO | MB_ICONQUESTION, NULL, _r_locale_getstring (IDS_QUESTION_STOP)) != IDYES)
				{
					SetWindowLongPtrW (hwnd, DWLP_MSGRESULT, TRUE);

					return TRUE;
				}
			}

			DestroyWindow (hwnd);

			break;
		}

		case WM_DESTROY:
		{
			_r_tray_destroy (hwnd, &GUID_TrayIcon);

			if (hicon_app_small)
			{
				DestroyIcon (hicon_app_small);
				hicon_app_small = NULL;
			}

			if (hicon_app_large)
			{
				DestroyIcon (hicon_app_large);
				hicon_app_large = NULL;
			}

			if (_r_config_getboolean (L"ChromiumRunAtEnd", TRUE))
			{
				if (browser_info.is_waitdownloadend && !browser_info.is_onlyupdate)
					_app_openbrowser (&browser_info);
			}

			//_r_workqueue_waitforfinish (&workqueue);
			//_r_workqueue_destroy (&workqueue);

			PostQuitMessage (0);

			break;
		}

		case WM_LBUTTONDOWN:
		{
			_r_wnd_sendmessage (hwnd, 0, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
			break;
		}

		case WM_ENTERSIZEMOVE:
		case WM_EXITSIZEMOVE:
		case WM_CAPTURECHANGED:
		{
			LONG_PTR ex_style;

			ex_style = _r_wnd_getstyle (hwnd, GWL_EXSTYLE);

			if ((ex_style & WS_EX_LAYERED) == 0)
				_r_wnd_setstyle (hwnd, WS_EX_LAYERED, WS_EX_LAYERED, GWL_EXSTYLE);

			SetLayeredWindowAttributes (hwnd, 0, (msg == WM_ENTERSIZEMOVE) ? 100 : 255, LWA_ALPHA);

			SetCursor (LoadCursorW (NULL, (msg == WM_ENTERSIZEMOVE) ? IDC_SIZEALL : IDC_ARROW));

			break;
		}

		case WM_NOTIFY:
		{
			LPNMHDR lpnmhdr;

			lpnmhdr = (LPNMHDR)lparam;

			switch (lpnmhdr->code)
			{
				case TTN_GETDISPINFO:
				{
					LPNMTTDISPINFOW lpnmdi;
					WCHAR buffer[1024];
					PR_STRING string;
					INT ctrl_id;

					lpnmdi = (LPNMTTDISPINFOW)lparam;

					if ((lpnmdi->uFlags & TTF_IDISHWND) == 0)
						break;

					ctrl_id = GetDlgCtrlID ((HWND)lpnmdi->hdr.idFrom);
					string = _r_ctrl_getstring (hwnd, ctrl_id);

					if (!string)
						break;

					_r_str_copy (buffer, RTL_NUMBER_OF (buffer), string->buffer);

					if (!_r_str_isempty (buffer))
						lpnmdi->lpszText = buffer;

					_r_obj_dereference (string);

					break;
				}

				case NM_CLICK:
				case NM_RETURN:
				{
					PNMLINK nmlink;

					nmlink = (PNMLINK)lparam;

					if (!_r_str_isempty (nmlink->item.szUrl))
						_r_shell_opendefault (nmlink->item.szUrl);

					break;
				}
			}

			break;
		}

		case RM_TRAYICON:
		{
			switch (LOWORD (lparam))
			{
				case NIN_BALLOONUSERCLICK:
				{
					_r_wnd_toggle (hwnd, TRUE);
					break;
				}

				case NIN_KEYSELECT:
				{
					if (GetForegroundWindow () != hwnd)
						_r_wnd_toggle (hwnd, FALSE);

					break;
				}

				case WM_MBUTTONUP:
				{
					_r_wnd_sendmessage (hwnd, 0, WM_COMMAND, MAKEWPARAM (IDM_EXPLORE, 0), 0);
					break;
				}

				case WM_LBUTTONUP:
				{
					_r_wnd_toggle (hwnd, FALSE);
					break;
				}

				case WM_CONTEXTMENU:
				{
					HMENU hmenu;
					HMENU hsubmenu;

					SetForegroundWindow (hwnd); // don't touch

					hmenu = LoadMenuW (NULL, MAKEINTRESOURCE (IDM_TRAY));

					if (!hmenu)
						break;

					hsubmenu = GetSubMenu (hmenu, 0);

					if (hsubmenu)
					{
						// localize
						_r_menu_setitemtext (hsubmenu, IDM_TRAY_SHOW, FALSE, _r_locale_getstring (IDS_TRAY_SHOW));
						_r_menu_setitemtextformat (hsubmenu, IDM_TRAY_RUN, FALSE, L"%s...", _r_locale_getstring (IDS_RUN));
						_r_menu_setitemtextformat (hsubmenu, IDM_TRAY_OPEN, FALSE, L"%s...", _r_locale_getstring (IDS_OPEN));
						_r_menu_setitemtext (hsubmenu, IDM_TRAY_WEBSITE, FALSE, _r_locale_getstring (IDS_WEBSITE));
						_r_menu_setitemtext (hsubmenu, IDM_TRAY_ABOUT, FALSE, _r_locale_getstring (IDS_ABOUT));
						_r_menu_setitemtext (hsubmenu, IDM_TRAY_EXIT, FALSE, _r_locale_getstring (IDS_EXIT));

						if (_r_obj_isstringempty (browser_info.binary_path) || !_r_fs_exists (&browser_info.binary_path->sr))
							_r_menu_enableitem (hsubmenu, IDM_TRAY_RUN, MF_BYCOMMAND, FALSE);

						_r_menu_popup (hsubmenu, hwnd, NULL, TRUE);
					}

					DestroyMenu (hmenu);

					break;
				}
			}

			break;
		}

		case WM_COMMAND:
		{
			INT ctrl_id = LOWORD (wparam);
			INT notify_code = HIWORD (wparam);

			if (HIWORD (wparam) == 0 && LOWORD (wparam) >= IDX_LANGUAGE && LOWORD (wparam) <= IDX_LANGUAGE + _r_locale_getcount () + 1)
			{
				HMENU hmenu;

				hmenu = GetMenu (hwnd);
				hmenu = GetSubMenu (hmenu, 1);
				hmenu = GetSubMenu (hmenu, LANG_MENU);

				_r_locale_apply (hmenu, LOWORD (wparam), IDX_LANGUAGE);

				return FALSE;
			}

			switch (ctrl_id)
			{
				case IDCANCEL: // process Esc key
				case IDM_TRAY_SHOW:
				{
					_r_wnd_toggle (hwnd, FALSE);
					break;
				}

				case IDM_EXIT:
				case IDM_TRAY_EXIT:
				{
					_r_wnd_sendmessage (hwnd, 0, WM_CLOSE, 0, 0);
					break;
				}

				case IDM_RUN:
				case IDM_TRAY_RUN:
				{
					_app_openbrowser (&browser_info);
					break;
				}

				case IDM_OPEN:
				case IDM_TRAY_OPEN:
				{
					PR_STRING path;

					path = _r_app_getconfigpath ();

					if (_r_fs_exists (&path->sr))
						_r_shell_opendefault (path->buffer);

					break;
				}

				case IDM_EXPLORE:
				{
					if (!browser_info.binary_dir)
						break;

					if (_r_fs_exists (&browser_info.binary_dir->sr))
						_r_shell_opendefault (browser_info.binary_dir->buffer);

					break;
				}

				case IDM_RUNATEND_CHK:
				{
					BOOLEAN new_val;

					new_val = !_r_config_getboolean (L"ChromiumRunAtEnd", TRUE);

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);

					_r_config_setboolean (L"ChromiumRunAtEnd", new_val);

					break;
				}

				case IDM_DARKMODE_CHK:
				{
					BOOLEAN new_val;

					new_val = !_r_theme_isenabled ();

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);

					_r_theme_enable (hwnd, new_val);

					break;
				}

				case IDM_TASKUPDATE_CHK:
				{
					BOOLEAN new_val;

					new_val = !_r_config_getboolean (L"TaskUpdateEnabled", FALSE);

					if (new_val)
					{
						if (_r_show_message (hwnd, MB_YESNO | MB_ICONQUESTION, NULL, _r_locale_getstring (IDS_QUESTION_TASKENABLE)) != IDYES)
							break;

						if (!_app_taskupdate_createtask ())
						{
							_r_show_message (hwnd, MB_ICONERROR, NULL, L"Failed to create scheduled task.");
							break;
						}
					}
					else
					{
						_app_taskupdate_deletetask ();
					}

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);
					_r_config_setboolean (L"TaskUpdateEnabled", new_val);

					break;
				}

				case IDM_CHECKFORUPDATES:
				{
					PR_STRING new_version = NULL;
					PR_STRING download_url = NULL;
					PR_STRING zip_path = NULL;
					PR_STRING msg = NULL;

					if (_app_check_self_update (hwnd, &new_version, &download_url))
					{
						msg = _r_format_string (_r_locale_getstring (IDS_UPDATE_AVAILABLE), new_version->buffer);

						if (msg && _r_show_message (hwnd, MB_YESNO | MB_ICONINFORMATION, NULL, msg->buffer) == IDYES)
						{
							if (_app_download_self_update (hwnd, download_url, &zip_path))
							{
								_app_apply_self_update (hwnd, zip_path);
								_r_obj_dereference (zip_path);
							}
							else
							{
								_r_show_message (hwnd, MB_OK | MB_ICONERROR, NULL, _r_locale_getstring (IDS_UPDATE_ERROR));
							}
						}

						if (msg)
							_r_obj_dereference (msg);
					}
					else
					{
						_r_show_message (hwnd, MB_OK | MB_ICONINFORMATION, NULL, _r_locale_getstring (IDS_UPDATE_UPTODATE));
					}

					if (new_version)
						_r_obj_dereference (new_version);

					if (download_url)
						_r_obj_dereference (download_url);

					break;
				}

				case IDM_AUTOCHECKUPDATES_CHK:
				{
					BOOLEAN new_val;

					new_val = !_r_config_getboolean (L"AutoCheckUpdates", TRUE);

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);
					_r_config_setboolean (L"AutoCheckUpdates", new_val);

					break;
				}

				case IDM_EXPORTPROFILE:
				{
					_app_export_profile (hwnd);
					break;
				}

				case IDM_IMPORTPROFILE:
				{
					_app_import_profile (hwnd);
					break;
				}

				case IDM_UNINSTALL:
				{
					_app_uninstall_app (hwnd);
					break;
				}

				case IDM_WEBSITE:
				case IDM_TRAY_WEBSITE:
				{
					_r_shell_opendefault (_r_app_getwebsite_url ());
					break;
				}

				case IDM_ABOUT:
				case IDM_TRAY_ABOUT:
				{
					_r_show_aboutmessage (hwnd);
					break;
				}

				case IDC_START_BTN:
				{
					_r_workqueue_queueitem (&workqueue, &_app_thread_check, &browser_info);
					break;
				}
			}

			break;
		}
	}
	return FALSE;
}

static VOID _app_auto_check_updates (
	_In_ HWND hwnd
)
{
	PR_STRING new_version = NULL;
	PR_STRING download_url = NULL;
	PR_STRING zip_path = NULL;
	PR_STRING msg = NULL;

	if (!_r_config_getboolean (L"AutoCheckUpdates", TRUE))
		return;

	if (_app_check_self_update (hwnd, &new_version, &download_url))
	{
		msg = _r_format_string (_r_locale_getstring (IDS_UPDATE_AVAILABLE), new_version->buffer);

		if (msg && _r_show_message (hwnd, MB_YESNO | MB_ICONINFORMATION, NULL, msg->buffer) == IDYES)
		{
			if (_app_download_self_update (hwnd, download_url, &zip_path))
			{
				_app_apply_self_update (hwnd, zip_path);
				_r_obj_dereference (zip_path);
			}
		}

		if (msg)
			_r_obj_dereference (msg);
	}

	if (new_version)
		_r_obj_dereference (new_version);

	if (download_url)
		_r_obj_dereference (download_url);
}

INT APIENTRY wWinMain (
	_In_ HINSTANCE hinst,
	_In_opt_ HINSTANCE prev_hinst,
	_In_ LPWSTR cmdline,
	_In_ INT show_cmd
)
{
	PR_STRING path;
	HWND hwnd;

	if (!_r_app_initialize (NULL))
		return ERROR_APP_INIT_FAILURE;

	_r_workqueue_initialize (&workqueue, 1, NULL, NULL);

	path = _r_app_getdirectory ();

	_r_fs_setcurrentdirectory (&path->sr);

	if (cmdline)
	{
		_app_init_browser_info (&browser_info);

		if (browser_info.is_hasurls && _r_fs_exists (&browser_info.binary_path->sr))
		{
			_app_openbrowser (&browser_info);

			return ERROR_SUCCESS;
		}
	}

	hwnd = _r_app_createwindow (hinst, MAKEINTRESOURCE (IDD_MAIN), MAKEINTRESOURCE (IDI_MAIN), &DlgProc);

	if (!hwnd)
		return ERROR_APP_INIT_FAILURE;

	// Check for chrlauncher migration
	if (_app_check_migration ())
	{
		if (_r_show_message (hwnd, MB_YESNO | MB_ICONQUESTION, _r_locale_getstring (IDS_MIGRATION_TITLE), _r_locale_getstring (IDS_MIGRATION_FOUND)) == IDYES)
		{
			_app_perform_migration (hwnd);
			return ERROR_SUCCESS;
		}
	}

	// Auto-check for app updates
	_app_auto_check_updates (hwnd);

	return _r_wnd_message_callback (hwnd, MAKEINTRESOURCE (IDA_MAIN));
}

