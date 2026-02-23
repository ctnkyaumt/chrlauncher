// chrlauncher
// Copyright (c) 2015-2025 Henry++

#include "routine.h"

#include "main.h"
#include "rapp.h"

extern R_QUEUED_LOCK lock_thread;

VOID _app_set_lastcheck (
	_In_ PBROWSER_INFORMATION pbi
);

VOID _app_create_profileshortcut (
	_In_ PBROWSER_INFORMATION pbi
);

VOID _app_init_browser_info (
	_Inout_ PBROWSER_INFORMATION pbi
);

VOID _app_update_browser_info (
	_In_ HWND hwnd,
	_In_ PBROWSER_INFORMATION pbi
);

VOID _app_taskupdate_closebrowser (
	_In_ PBROWSER_INFORMATION pbi,
	_Out_ PBOOLEAN was_running_ptr
);

VOID _app_openbrowser (
	_In_ PBROWSER_INFORMATION pbi
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
);

BOOLEAN _app_is_instance_configured (
	_In_ LONG instance_id
)
{
	PR_STRING type_value = NULL;
	PR_STRING type_key = NULL;
	BOOLEAN is_configured = FALSE;

	if (instance_id <= 1)
		return TRUE;

	type_key = _r_format_string (L"ChromiumType%" TEXT (PR_LONG), instance_id);

	if (!type_key)
		return FALSE;

	type_value = _r_config_getstring (type_key->buffer, NULL);

	if (type_value && !_r_obj_isstringempty (type_value))
		is_configured = TRUE;

	if (type_value)
		_r_obj_dereference (type_value);

	_r_obj_dereference (type_key);

	return is_configured;
}

VOID _app_clear_browser_info_references (
	_Inout_ PBROWSER_INFORMATION pbi
)
{
	_r_obj_clearreference ((PVOID_PTR)&pbi->args_str);
	_r_obj_clearreference ((PVOID_PTR)&pbi->urls_str);
	_r_obj_clearreference ((PVOID_PTR)&pbi->chrome_plus_dir);
	_r_obj_clearreference ((PVOID_PTR)&pbi->browser_name);
	_r_obj_clearreference ((PVOID_PTR)&pbi->browser_type);
	_r_obj_clearreference ((PVOID_PTR)&pbi->cache_path);
	_r_obj_clearreference ((PVOID_PTR)&pbi->binary_dir);
	_r_obj_clearreference ((PVOID_PTR)&pbi->binary_path);
	_r_obj_clearreference ((PVOID_PTR)&pbi->profile_dir);
	_r_obj_clearreference ((PVOID_PTR)&pbi->download_url);
	_r_obj_clearreference ((PVOID_PTR)&pbi->current_version);
	_r_obj_clearreference ((PVOID_PTR)&pbi->new_version);
}

VOID _app_update_secondary_instance (
	_In_ HWND hwnd,
	_Inout_ PBROWSER_INFORMATION pbi
)
{
	BOOLEAN is_haveerror = FALSE;
	BOOLEAN is_exists;
	BOOLEAN is_updaterequired;

	if (!pbi || !pbi->binary_path)
		return;

	_app_update_browser_info (hwnd, pbi);

	if (_app_isupdatedownloaded (pbi))
	{
		if (!_r_fs_isfileused (&pbi->binary_path->sr))
		{
			if (_app_installupdate (hwnd, pbi, &is_haveerror))
			{
				_app_init_browser_info (pbi);
				_app_set_lastcheck (pbi);
				_app_create_profileshortcut (pbi);
			}
		}

		return;
	}

	is_exists = _r_fs_exists (&pbi->binary_path->sr);
	is_updaterequired = _app_isupdaterequired (pbi);

	if (is_exists && !is_updaterequired)
	{
		_app_create_profileshortcut (pbi);
		return;
	}

	if (!_app_checkupdate (hwnd, pbi, &is_haveerror) || is_haveerror)
		return;

	if (!_app_ishaveupdate (pbi))
		return;

	if (is_exists && !pbi->is_autodownload)
		return;

	if (!_app_downloadupdate (hwnd, pbi, &is_haveerror) || is_haveerror)
		return;

	if (_r_fs_isfileused (&pbi->binary_path->sr))
		return;

	if (_app_installupdate (hwnd, pbi, &is_haveerror))
	{
		_app_init_browser_info (pbi);
		_app_set_lastcheck (pbi);
		_app_create_profileshortcut (pbi);
	}
}

VOID _app_thread_taskupdate_all (
	_In_ HWND hwnd,
	_Inout_ PBROWSER_INFORMATION primary
)
{
	BROWSER_INFORMATION instances[4] = {0};
	BOOLEAN was_running[4] = {0};
	PR_STRING saved_args[4] = {0};
	PR_STRING restore_args = NULL;
	BOOLEAN is_haveerror = FALSE;

	for (LONG instance_id = 1; instance_id <= 4; instance_id++)
	{
		PBROWSER_INFORMATION pbi = NULL;

		if (!_app_is_instance_configured (instance_id))
			continue;

		if (primary->instance_id == instance_id)
		{
			pbi = primary;
		}
		else
		{
			pbi = &instances[instance_id - 1];
			RtlZeroMemory (pbi, sizeof (*pbi));
			pbi->hwnd = primary->hwnd;
			pbi->htaskbar = primary->htaskbar;
			pbi->instance_id = instance_id;
			pbi->architecture = 0;
			pbi->is_taskupdate = TRUE;
			pbi->is_forcecheck = TRUE;
			pbi->is_autodownload = TRUE;
			_app_init_browser_info (pbi);
		}

		_app_update_browser_info (hwnd, pbi);

		_app_taskupdate_closebrowser (pbi, &was_running[instance_id - 1]);

		if (!_app_isupdatedownloaded (pbi))
		{
			if (!_app_checkupdate (hwnd, pbi, &is_haveerror) || is_haveerror)
				continue;

			if (!_app_ishaveupdate (pbi))
				continue;

			if (!_app_downloadupdate (hwnd, pbi, &is_haveerror) || is_haveerror)
				continue;
		}

		for (INT i = 0; i < 14; i++)
		{
			if (!_r_fs_isfileused (&pbi->binary_path->sr))
			{
				if (_app_installupdate (hwnd, pbi, &is_haveerror))
				{
					_app_set_lastcheck (pbi);
					_app_create_profileshortcut (pbi);
				}

				break;
			}

			Sleep (30000);
		}
	}

	for (LONG instance_id = 1; instance_id <= 4; instance_id++)
	{
		PBROWSER_INFORMATION pbi = NULL;

		if (!was_running[instance_id - 1])
			continue;

		if (primary->instance_id == instance_id)
		{
			pbi = primary;
		}
		else
		{
			pbi = &instances[instance_id - 1];
		}

		if (!pbi->args_str)
			continue;

		restore_args = _r_obj_concatstrings (2, _r_obj_getstring (pbi->args_str), L" --restore-last-session");

		if (restore_args)
		{
			_r_obj_movereference ((PVOID_PTR)&saved_args[instance_id - 1], pbi->args_str);
			_r_obj_movereference ((PVOID_PTR)&pbi->args_str, restore_args);
		}

		_app_openbrowser (pbi);

		if (saved_args[instance_id - 1])
			_r_obj_movereference ((PVOID_PTR)&pbi->args_str, saved_args[instance_id - 1]);
	}

	{
		LONG primary_index = primary->instance_id;

		if (primary_index >= 1 && primary_index <= 4)
		{
			if (!was_running[primary_index - 1])
				_app_openbrowser (primary);
		}
		else
		{
			_app_openbrowser (primary);
		}
	}

	for (ULONG_PTR i = 0; i < RTL_NUMBER_OF (instances); i++)
		_app_clear_browser_info_references (&instances[i]);

	_r_progress_setmarquee (hwnd, IDC_PROGRESS, FALSE);
	_r_queuedlock_releaseshared (&lock_thread);
	_r_wnd_sendmessage (hwnd, 0, WM_DESTROY, 0, 0);
}
