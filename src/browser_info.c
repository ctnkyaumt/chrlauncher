// chrlauncher
// Copyright (c) 2015-2025 Henry++

#include "routine.h"

#include "main.h"
#include "rapp.h"

#include <stdlib.h>

VOID _app_parse_args (
	_Inout_ PBROWSER_INFORMATION pbi
);

static PR_STRING _app_get_arch_root_dir (
	_In_ LONG architecture
)
{
	WCHAR exe_path[4096] = {0};
	WCHAR dir_buf[4096] = {0};
	WCHAR parent_buf[4096] = {0};
	LPCWSTR arch_folder = (architecture == 32) ? L"32" : L"64";
	PWSTR slash_ptr;
	PWSTR leaf_ptr;
	PR_STRING preferred_root = NULL;
	PR_STRING legacy_root = NULL;
	PR_STRING preferred_bin = NULL;
	PR_STRING legacy_bin = NULL;

	if (!GetModuleFileNameW (NULL, exe_path, RTL_NUMBER_OF (exe_path)))
		return _r_obj_createstring (L".");

	_r_str_copy (dir_buf, RTL_NUMBER_OF (dir_buf), exe_path);

	slash_ptr = wcsrchr (dir_buf, L'\\');

	if (slash_ptr)
		*slash_ptr = 0;

	leaf_ptr = wcsrchr (dir_buf, L'\\');

	if (leaf_ptr)
		leaf_ptr = leaf_ptr + 1;
	else
		leaf_ptr = dir_buf;

	if (_r_str_compare (leaf_ptr, L"32", 2) == 0 || _r_str_compare (leaf_ptr, L"64", 2) == 0)
	{
		_r_str_copy (parent_buf, RTL_NUMBER_OF (parent_buf), dir_buf);

		slash_ptr = wcsrchr (parent_buf, L'\\');

		if (slash_ptr)
			*slash_ptr = 0;

		preferred_root = _r_format_string (L"%s\\%s", parent_buf, arch_folder);
		legacy_root = _r_format_string (L"%s\\%s", dir_buf, arch_folder);
	}
	else
	{
		preferred_root = _r_format_string (L"%s\\%s", dir_buf, arch_folder);
	}

	if (!preferred_root)
	{
		if (legacy_root)
			_r_obj_dereference (legacy_root);

		return NULL;
	}

	if (!legacy_root)
		return preferred_root;

	preferred_bin = _r_format_string (L"%s\\bin", preferred_root->buffer);
	legacy_bin = _r_format_string (L"%s\\bin", legacy_root->buffer);

	if (preferred_bin && legacy_bin)
	{
		if (!_r_fs_exists (&preferred_bin->sr) && _r_fs_exists (&legacy_bin->sr))
		{
			_r_obj_dereference (preferred_root);
			preferred_root = legacy_root;
			legacy_root = NULL;
		}
	}

	if (preferred_bin)
		_r_obj_dereference (preferred_bin);

	if (legacy_bin)
		_r_obj_dereference (legacy_bin);

	if (legacy_root)
		_r_obj_dereference (legacy_root);

	return preferred_root;
}

static PR_STRING _app_get_instance_dir (
	_In_ LONG architecture,
	_In_ LONG instance_id
)
{
	PR_STRING arch_root;
	PR_STRING instance_dir;

	arch_root = _app_get_arch_root_dir (architecture);

	if (!arch_root)
		return NULL;

	if (instance_id <= 1)
		instance_dir = _r_format_string (L"%s\\bin", arch_root->buffer);
	else
		instance_dir = _r_format_string (L"%s\\bin%" TEXT (PR_LONG), arch_root->buffer, instance_id);

	_r_obj_dereference (arch_root);

	return instance_dir;
}

static PR_STRING _app_get_profile_dir (
	_In_ LONG architecture,
	_In_ LONG instance_id
)
{
	PR_STRING arch_root;
	PR_STRING profile_dir;

	arch_root = _app_get_arch_root_dir (architecture);

	if (!arch_root)
		return NULL;

	if (instance_id <= 1)
		profile_dir = _r_format_string (L"%s\\profile", arch_root->buffer);
	else
		profile_dir = _r_format_string (L"%s\\profile%" TEXT (PR_LONG), arch_root->buffer, instance_id);

	_r_obj_dereference (arch_root);

	return profile_dir;
}

static LONG _app_get_next_instance_id (
	_In_ LONG architecture
)
{
	PR_STRING dir;
	LONG instance_id = 1;

	for (; instance_id < 1000; instance_id++)
	{
		dir = _app_get_instance_dir (architecture, instance_id);

		if (!dir)
			break;

		if (!_r_fs_exists (&dir->sr))
		{
			_r_obj_dereference (dir);
			break;
		}

		_r_obj_dereference (dir);
	}

	if (instance_id < 1)
		instance_id = 1;

	return instance_id;
}

static PR_STRING _app_apply_profile_dir (
	_In_opt_ PR_STRING args,
	_In_ PR_STRING profile_dir
)
{
	static const WCHAR FLAG_SWITCHES_END[] = L"--flag-switches-end";
	static const WCHAR USER_DATA_DIR[] = L"--user-data-dir";

	PR_STRING profile_arg;
	PR_STRING result;
	LPCWSTR existing_ptr;
	LPCWSTR replace_start;
	LPCWSTR replace_end;
	LPCWSTR args_end;
	LPCWSTR end_ptr;
	LPCWSTR suffix_ptr;
	SIZE_T prefix_length;
	SIZE_T suffix_length;
	SIZE_T result_length;
	BOOLEAN is_quote = FALSE;

	if (!profile_dir || _r_obj_isstringempty (profile_dir))
		return args ? _r_obj_createstring (args->buffer) : NULL;

	profile_arg = _r_format_string (L"--user-data-dir=\"%s\"", profile_dir->buffer);

	if (!profile_arg)
		return args ? _r_obj_createstring (args->buffer) : NULL;

	if (!args)
		return profile_arg;

	existing_ptr = StrStrIW (args->buffer, USER_DATA_DIR);

	if (existing_ptr)
	{
		replace_start = existing_ptr;

		if (replace_start > args->buffer && *(replace_start - 1) == L' ')
			replace_start--;

		replace_end = existing_ptr;
		args_end = args->buffer + (args->length / sizeof (WCHAR));

		while (replace_end < args_end && *replace_end)
		{
			if (*replace_end == L'"')
				is_quote = !is_quote;

			if (!is_quote && (*replace_end == L' ' || *replace_end == L'\t'))
				break;

			replace_end++;
		}

		suffix_ptr = replace_end;

		while (*suffix_ptr == L' ' || *suffix_ptr == L'\t')
			suffix_ptr++;

		prefix_length = (SIZE_T)(replace_start - args->buffer) * sizeof (WCHAR);
		suffix_length = (SIZE_T)(args_end - suffix_ptr) * sizeof (WCHAR);

		result_length = prefix_length +
			(prefix_length ? sizeof (WCHAR) : 0) +
			(SIZE_T)profile_arg->length +
			(suffix_length ? sizeof (WCHAR) : 0) +
			suffix_length;

		result = _r_obj_createstring_ex (NULL, result_length);

		if (result)
		{
			SIZE_T offset = 0;

			if (prefix_length)
			{
				RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, offset), args->buffer, prefix_length);
				offset += prefix_length;

				RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, offset), L" ", sizeof (WCHAR));
				offset += sizeof (WCHAR);
			}

			RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, offset), profile_arg->buffer, profile_arg->length);
			offset += profile_arg->length;

			if (suffix_length)
			{
				RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, offset), L" ", sizeof (WCHAR));
				offset += sizeof (WCHAR);

				RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, offset), suffix_ptr, suffix_length);
			}

			_r_str_trimtonullterminator (&result->sr);
		}

		_r_obj_dereference (profile_arg);

		return result ? result : _r_obj_createstring (args->buffer);
	}

	end_ptr = StrStrIW (args->buffer, FLAG_SWITCHES_END);

	if (end_ptr)
	{
		prefix_length = (SIZE_T)(end_ptr - args->buffer) * sizeof (WCHAR);
		suffix_length = (SIZE_T)args->length - prefix_length;
		result_length = (SIZE_T)args->length + (SIZE_T)profile_arg->length + (2 * sizeof (WCHAR));

		result = _r_obj_createstring_ex (NULL, result_length);

		if (result)
		{
			RtlCopyMemory (result->buffer, args->buffer, prefix_length);
			RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, prefix_length), L" ", sizeof (WCHAR));
			RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, prefix_length + sizeof (WCHAR)), profile_arg->buffer, profile_arg->length);
			RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, prefix_length + sizeof (WCHAR) + profile_arg->length), L" ", sizeof (WCHAR));
			RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, prefix_length + (2 * sizeof (WCHAR)) + profile_arg->length), end_ptr, suffix_length);
			_r_str_trimtonullterminator (&result->sr);
		}
	}
	else
	{
		result_length = (SIZE_T)args->length + (SIZE_T)profile_arg->length + sizeof (WCHAR);

		result = _r_obj_createstring_ex (NULL, result_length);

		if (result)
		{
			RtlCopyMemory (result->buffer, args->buffer, args->length);
			RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, args->length), L" ", sizeof (WCHAR));
			RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, args->length + sizeof (WCHAR)), profile_arg->buffer, profile_arg->length);
			_r_str_trimtonullterminator (&result->sr);
		}
	}

	_r_obj_dereference (profile_arg);

	return result ? result : _r_obj_createstring (args->buffer);
}

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

	PR_STRING browser_arguments = NULL;
	PR_STRING browser_type = NULL;
	PR_STRING binary_dir;
	PR_STRING binary_name;
	PR_STRING string;
	PR_STRING profile_dir;
	USHORT architecture;
	NTSTATUS status;

	pbi->is_hasurls = FALSE;
	pbi->is_newinstance = FALSE;

	_r_obj_clearreference ((PVOID_PTR)&pbi->urls_str);
	_r_obj_clearreference ((PVOID_PTR)&pbi->args_str);
	_r_obj_clearreference ((PVOID_PTR)&pbi->profile_dir);

	_app_parse_args (pbi);

	binary_dir = _r_config_getstringexpand (L"ChromiumDirectory", L".\\bin");
	binary_name = _r_config_getstring (L"ChromiumBinary", L"chrome.exe");

	if (!binary_dir || !binary_name)
	{
		RtlRaiseStatus (STATUS_INVALID_PARAMETER);

		return;
	}

	if (pbi->instance_id < 1)
		pbi->instance_id = _r_config_getlong (L"ChromiumInstance", 1);

	if (pbi->instance_id < 1)
		pbi->instance_id = 1;

	if (pbi->architecture != 64 && pbi->architecture != 32)
		pbi->architecture = _r_config_getlong (L"ChromiumArchitecture", 0);

	if (pbi->architecture != 64 && pbi->architecture != 32)
	{
		PR_STRING test_dir;
		PR_STRING test_path;
		R_STRINGREF separator_sr = PR_STRINGREF_INIT (L"\\");
		LONG arch_test[] = {64, 32};

		for (ULONG_PTR a = 0; a < RTL_NUMBER_OF (arch_test); a++)
		{
			LONG arch_value = arch_test[a];

			test_dir = _app_get_instance_dir (arch_value, pbi->instance_id);

			if (!test_dir)
				continue;

			test_path = _r_obj_concatstringrefs (3, &test_dir->sr, &separator_sr, &binary_name->sr);

			if (test_path && _r_fs_exists (&test_path->sr))
			{
				pbi->architecture = arch_value;
			}

			if (test_path)
				_r_obj_dereference (test_path);

			_r_obj_dereference (test_dir);

			if (pbi->architecture == arch_value)
				break;
		}
	}

	if (pbi->architecture != 64 && pbi->architecture != 32)
	{
		status = _r_sys_getprocessorinformation (&architecture, NULL, NULL);

		if (NT_SUCCESS (status))
			pbi->architecture = (architecture == PROCESSOR_ARCHITECTURE_AMD64) ? 64 : 32;
	}

	if (pbi->architecture != 32 && pbi->architecture != 64)
		pbi->architecture = 64;

	if (pbi->is_newinstance)
		pbi->instance_id = _app_get_next_instance_id (pbi->architecture);

	string = _app_get_instance_dir (pbi->architecture, pbi->instance_id);
	profile_dir = _app_get_profile_dir (pbi->architecture, pbi->instance_id);

	_r_obj_dereference (binary_dir);

	if (!string || !profile_dir)
	{
		_r_obj_dereference (binary_name);

		if (string)
			_r_obj_dereference (string);

		if (profile_dir)
			_r_obj_dereference (profile_dir);

		RtlRaiseStatus (STATUS_INSUFFICIENT_RESOURCES);

		return;
	}

	status = _r_path_getfullpath (string->buffer, &binary_dir);

	if (NT_SUCCESS (status))
	{
		_r_obj_movereference ((PVOID_PTR)&pbi->binary_dir, binary_dir);
		_r_obj_dereference (string);
	}
	else
	{
		_r_obj_movereference ((PVOID_PTR)&pbi->binary_dir, string);
	}

	status = _r_path_getfullpath (profile_dir->buffer, &binary_dir);

	if (NT_SUCCESS (status))
	{
		_r_obj_movereference ((PVOID_PTR)&pbi->profile_dir, binary_dir);
		_r_obj_dereference (profile_dir);
	}
	else
	{
		_r_obj_movereference ((PVOID_PTR)&pbi->profile_dir, profile_dir);
	}

	_r_str_trimstring2 (&pbi->binary_dir->sr, L"\\", 0);
	_r_str_trimstring2 (&pbi->profile_dir->sr, L"\\", 0);

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

	if (binary_dir && (_r_str_compare (binary_dir->buffer, L".\\bin", 5) == 0 || _r_str_compare (binary_dir->buffer, L"bin", 3) == 0))
	{
		_r_obj_dereference (binary_dir);
		binary_dir = _r_obj_createstring (pbi->binary_dir->buffer);
	}

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

	if (pbi->instance_id >= 2 && pbi->instance_id <= 4)
	{
		PR_STRING type_key;

		type_key = _r_format_string (L"ChromiumType%" TEXT (PR_LONG), pbi->instance_id);

		if (type_key)
		{
			browser_type = _r_config_getstring (type_key->buffer, NULL);

			_r_obj_dereference (type_key);
		}
	}

	if (!browser_type)
		browser_type = _r_config_getstring (L"ChromiumType", CHROMIUM_TYPE);

	browser_arguments = _r_config_getstringexpand (L"ChromiumCommandLine", CHROMIUM_COMMAND_LINE);

	if (browser_type)
		_r_obj_movereference ((PVOID_PTR)&pbi->browser_type, browser_type);

	if (browser_arguments)
		_r_obj_movereference ((PVOID_PTR)&pbi->args_str, browser_arguments);

	if (pbi->profile_dir)
	{
		PR_STRING updated_args;

		updated_args = _app_apply_profile_dir (pbi->args_str, pbi->profile_dir);

		if (updated_args)
			_r_obj_movereference ((PVOID_PTR)&pbi->args_str, updated_args);
	}

	string = _r_format_string (L"%s (%" TEXT (PR_LONG) L"-bit)", pbi->browser_type->buffer, pbi->architecture);

	_r_obj_movereference ((PVOID_PTR)&pbi->browser_name, string);

	_r_obj_movereference ((PVOID_PTR)&pbi->current_version, _r_res_queryversionstring (pbi->binary_path->buffer));

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
