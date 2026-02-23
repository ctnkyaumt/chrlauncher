// chrlauncher
// Copyright (c) 2015-2025 Henry++

#include "routine.h"

#include "main.h"
#include "rapp.h"

VOID _app_parse_args (
	_Inout_ PBROWSER_INFORMATION pbi
);

VOID _app_init_browser_info (
	_Inout_ PBROWSER_INFORMATION pbi
)
{
	R_STRINGREF bin_names[] = {
		PR_STRINGREF_INIT (L"brave.exe"),
		PR_STRINGREF_INIT (L"firefox.exe"),
		PR_STRINGREF_INIT (L"basilisk.exe"),
		PR_STRINGREF_INIT (L"palemoon.exe"),
		PR_STRINGREF_INIT (L"waterfox.exe"),
		PR_STRINGREF_INIT (L"dragon.exe"),
		PR_STRINGREF_INIT (L"iridium.exe"),
		PR_STRINGREF_INIT (L"iron.exe"),
		PR_STRINGREF_INIT (L"opera.exe"),
		PR_STRINGREF_INIT (L"slimjet.exe"),
		PR_STRINGREF_INIT (L"vivaldi.exe"),
		PR_STRINGREF_INIT (L"chromium.exe"),
		PR_STRINGREF_INIT (L"chrome.exe"), // default
	};

	R_STRINGREF separator_sr = PR_STRINGREF_INIT (L"\\");

	PR_STRING browser_arguments;
	PR_STRING browser_type;
	PR_STRING binary_dir;
	PR_STRING binary_name;
	PR_STRING string;
	ULONG binary_type;
	USHORT architecture;
	NTSTATUS status;

	pbi->is_hasurls = FALSE;

	_r_obj_clearreference ((PVOID_PTR)&pbi->urls_str);
	_r_obj_clearreference ((PVOID_PTR)&pbi->args_str);

	binary_dir = _r_config_getstringexpand (L"ChromiumDirectory", L".\\bin");
	binary_name = _r_config_getstring (L"ChromiumBinary", L"chrome.exe");

	if (!binary_dir || !binary_name)
	{
		RtlRaiseStatus (STATUS_INVALID_PARAMETER);

		return;
	}

	status = _r_path_getfullpath (binary_dir->buffer, &string);

	if (NT_SUCCESS (status))
	{
		_r_obj_movereference ((PVOID_PTR)&pbi->binary_dir, string);

		_r_obj_dereference (binary_dir);
	}
	else
	{
		_r_obj_movereference ((PVOID_PTR)&pbi->binary_dir, binary_dir);
	}

	_r_str_trimstring2 (&pbi->binary_dir->sr, L"\\", 0);

	string = _r_obj_concatstringrefs (
		3,
		&pbi->binary_dir->sr,
		&separator_sr,
		&binary_name->sr
	);

	_r_obj_movereference ((PVOID_PTR)&pbi->binary_path, string);

	if (!_r_fs_exists (&pbi->binary_path->sr))
	{
		for (ULONG_PTR i = 0; i < RTL_NUMBER_OF (bin_names); i++)
		{
			string = _r_obj_concatstringrefs (
				3,
				&pbi->binary_dir->sr,
				&separator_sr,
				&bin_names[i]
			);

			_r_obj_movereference ((PVOID_PTR)&pbi->binary_path, string);

			if (_r_fs_exists (&pbi->binary_path->sr))
				break;
		}

		if (_r_obj_isstringempty (pbi->binary_path) || !_r_fs_exists (&pbi->binary_path->sr))
		{
			string = _r_obj_concatstringrefs (
				3,
				&pbi->binary_dir->sr,
				&separator_sr,
				&binary_name->sr
			);

			_r_obj_movereference ((PVOID_PTR)&pbi->binary_path, string);
		}
	}

	_r_obj_dereference (binary_name);

	binary_dir = _r_config_getstringexpand (L"ChromePlusDirectory", L".\\bin");

	status = _r_path_getfullpath (binary_dir->buffer, &string);

	if (NT_SUCCESS (status))
	{
		_r_obj_movereference ((PVOID_PTR)&pbi->chrome_plus_dir, string);

		_r_obj_dereference (binary_dir);
	}
	else
	{
		_r_obj_movereference ((PVOID_PTR)&pbi->chrome_plus_dir, binary_dir);
	}

	binary_dir = _r_sys_gettempdirectory ();

	string = _r_format_string (L"%s\\%s_%" TEXT (PR_ULONG) L".bin", _r_obj_getstring (binary_dir), _r_app_getnameshort (), _r_str_gethash2 (&pbi->binary_path->sr, TRUE));

	_r_obj_movereference ((PVOID_PTR)&pbi->cache_path, string);

	_r_obj_dereference (binary_dir);

	pbi->architecture = _r_config_getlong (L"ChromiumArchitecture", 0);

	if (pbi->architecture != 64 && pbi->architecture != 32)
	{
		pbi->architecture = 0;

		if (_r_fs_exists (&pbi->binary_path->sr))
		{
			status = _r_sys_getbinarytype (&pbi->binary_path->sr, &binary_type);

			if (NT_SUCCESS (status))
				pbi->architecture = (binary_type == SCS_64BIT_BINARY) ? 64 : 32;
		}

		if (!pbi->architecture)
		{
			status = _r_sys_getprocessorinformation (&architecture, NULL, NULL);

			if (NT_SUCCESS (status))
				pbi->architecture = (architecture == PROCESSOR_ARCHITECTURE_AMD64) ? 64 : 32;
		}
	}

	if (pbi->architecture != 32 && pbi->architecture != 64)
		pbi->architecture = 64;

	browser_type = _r_config_getstring (L"ChromiumType", CHROMIUM_TYPE);
	browser_arguments = _r_config_getstringexpand (L"ChromiumCommandLine", CHROMIUM_COMMAND_LINE);

	if (browser_type)
		_r_obj_movereference ((PVOID_PTR)&pbi->browser_type, browser_type);

	if (browser_arguments)
		_r_obj_movereference ((PVOID_PTR)&pbi->args_str, browser_arguments);

	string = _r_format_string (L"%s (%" TEXT (PR_LONG) L"-bit)", pbi->browser_type->buffer, pbi->architecture);

	_r_obj_movereference ((PVOID_PTR)&pbi->browser_name, string);

	_r_obj_movereference ((PVOID_PTR)&pbi->current_version, _r_res_queryversionstring (pbi->binary_path->buffer));

	_app_parse_args (pbi);

	pbi->check_period = _r_config_getlong (L"ChromiumCheckPeriod", 0);

	if (pbi->check_period == INT_ERROR)
		pbi->is_forcecheck = TRUE;

	if (!pbi->is_autodownload)
		pbi->is_autodownload = _r_config_getboolean (L"ChromiumAutoDownload", FALSE);

	if (!pbi->is_bringtofront)
		pbi->is_bringtofront = _r_config_getboolean (L"ChromiumBringToFront", TRUE);

	if (!pbi->is_waitdownloadend)
		pbi->is_waitdownloadend = _r_config_getboolean (L"ChromiumWaitForDownloadEnd", TRUE);

	if (!pbi->is_onlyupdate)
		pbi->is_onlyupdate = _r_config_getboolean (L"ChromiumUpdateOnly", TRUE);

	if (pbi->is_onlyupdate)
	{
		pbi->is_forcecheck = TRUE;
		pbi->is_bringtofront = TRUE;
		pbi->is_waitdownloadend = TRUE;
	}

	if (pbi->is_taskupdate)
	{
		pbi->is_onlyupdate = FALSE;
		pbi->is_autodownload = TRUE;
		pbi->is_forcecheck = TRUE;
		pbi->is_bringtofront = FALSE;
		pbi->is_waitdownloadend = TRUE;
	}
}
