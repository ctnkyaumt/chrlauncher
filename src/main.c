// chrlauncher
// Copyright (c) 2015-2025 Henry++

#include "routine.h"

#include "main.h"
#include "rapp.h"

#include "resource.h"

BROWSER_INFORMATION browser_info = {0};

R_QUEUED_LOCK lock_download = PR_QUEUED_LOCK_INIT;
R_QUEUED_LOCK lock_thread = PR_QUEUED_LOCK_INIT;

R_WORKQUEUE workqueue;

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

BOOLEAN _app_taskupdate_istaskpresent ();
BOOLEAN _app_taskupdate_setstartwhenavailable ();
BOOLEAN _app_taskupdate_createtask ();
BOOLEAN _app_taskupdate_deletetask ();

VOID _app_update_browser_info (
	_In_ HWND hwnd,
	_In_ PBROWSER_INFORMATION pbi
);

VOID _app_setstatus (
	_In_ HWND hwnd,
	_In_opt_ HWND htaskbar,
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
				_r_tray_popupformat (hwnd, &GUID_TrayIcon, NIIF_ERROR, _r_app_getname (), _r_locale_getstring (IDS_STATUS_FOUND), pbi->new_version->buffer); // just inform user

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

			_app_setstatus (hwnd, pbi->htaskbar, _r_locale_getstring (IDS_STATUS_ERROR), 0, 0);
		}

		is_stayopen = TRUE;
	}

	if (_r_fs_exists (&pbi->binary_path->sr))
		_app_create_profileshortcut (pbi);

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

			hicon = _r_sys_loadsharedicon (_r_sys_getimagebase (), MAKEINTRESOURCE (IDI_MAIN), icon_small);

			browser_info.hwnd = hwnd;

			_app_init_browser_info (&browser_info);

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

			hicon = _r_sys_loadsharedicon (_r_sys_getimagebase (), MAKEINTRESOURCE (IDI_MAIN), icon_small);

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

			hicon = _r_sys_loadsharedicon (_r_sys_getimagebase (), MAKEINTRESOURCE (IDI_MAIN), icon_small);

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

	return _r_wnd_message_callback (hwnd, MAKEINTRESOURCE (IDA_MAIN));
}
