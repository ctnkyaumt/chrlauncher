// launchbro
// 2026 ctnkyaumt

#include "routine.h"

#include "main.h"
#include "rapp.h"

#include "resource.h"
#include <shellapi.h>

#define LAUNCHBRO_RELEASE_API L"https://api.github.com/repos/ctnkyaumt/launchbro/releases/latest"
#define LAUNCHBRO_UPDATE_CONFIG_KEY L"LaunchbroUpdateUrl"
#define SELF_UPDATE_MUTEX L"launchbro_selfupdate"

extern R_QUEUED_LOCK lock_download;
extern R_QUEUED_LOCK lock_thread;

static PR_STRING _app_json_read_string (
	_In_ LPCWSTR value_ptr
)
{
	BOOLEAN is_escaped = FALSE;
	LPCWSTR start;
	LPCWSTR end;

	if (!value_ptr || value_ptr[0] != L'"')
		return NULL;

	start = value_ptr + 1;
	end = start;

	while (end[0])
	{
		if (is_escaped)
		{
			is_escaped = FALSE;
			end += 1;
			continue;
		}

		if (end[0] == L'\\')
		{
			is_escaped = TRUE;
			end += 1;
			continue;
		}

		if (end[0] == L'"')
			break;

		end += 1;
	}

	if (end[0] != L'"')
		return NULL;

	return _r_obj_createstring_ex (start, (ULONG_PTR)(end - start) * sizeof (WCHAR));
}

static LPCWSTR _app_json_skip_whitespace (
	_In_ LPCWSTR ptr
)
{
	if (!ptr)
		return NULL;

	while (ptr[0] == L' ' || ptr[0] == L'\t' || ptr[0] == L'\r' || ptr[0] == L'\n')
		ptr += 1;

	return ptr;
}

static PR_STRING _app_json_find_string_value (
	_In_ LPCWSTR json,
	_In_ LPCWSTR key
)
{
	WCHAR pattern[128] = {0};
	LPCWSTR key_ptr;
	LPCWSTR colon_ptr;
	LPCWSTR value_ptr;

	if (!json || !key || !key[0])
		return NULL;

	swprintf_s (pattern, RTL_NUMBER_OF (pattern), L"\"%s\"", key);

	key_ptr = wcsstr (json, pattern);

	if (!key_ptr)
		return NULL;

	colon_ptr = wcschr (key_ptr + 1, L':');

	if (!colon_ptr)
		return NULL;

	value_ptr = _app_json_skip_whitespace (colon_ptr + 1);

	if (!value_ptr || value_ptr[0] != L'"')
		return NULL;

	return _app_json_read_string (value_ptr);
}

static BOOLEAN _app_wcs_endswith (
	_In_ LPCWSTR string,
	_In_ LPCWSTR suffix
)
{
	SIZE_T string_len;
	SIZE_T suffix_len;

	if (!string || !suffix)
		return FALSE;

	string_len = wcslen (string);
	suffix_len = wcslen (suffix);

	if (suffix_len == 0 || string_len < suffix_len)
		return FALSE;

	return _wcsicmp (string + (string_len - suffix_len), suffix) == 0;
}

static BOOLEAN _app_wcs_contains_i (
	_In_ LPCWSTR haystack,
	_In_ LPCWSTR needle
)
{
	SIZE_T hay_len;
	SIZE_T needle_len;

	if (!haystack || !needle || !needle[0])
		return FALSE;

	hay_len = wcslen (haystack);
	needle_len = wcslen (needle);

	if (hay_len < needle_len)
		return FALSE;

	for (SIZE_T i = 0; i + needle_len <= hay_len; i++)
	{
		if (_wcsnicmp (haystack + i, needle, needle_len) == 0)
			return TRUE;
	}

	return FALSE;
}

static PR_STRING _app_get_app_root_dir ()
{
	WCHAR exe_path[4096] = {0};
	WCHAR dir_buf[4096] = {0};
	PWSTR slash_ptr;

	if (!GetModuleFileNameW (NULL, exe_path, RTL_NUMBER_OF (exe_path)))
		return _r_obj_createstring (L".");

	_r_str_copy (dir_buf, RTL_NUMBER_OF (dir_buf), exe_path);

	slash_ptr = wcsrchr (dir_buf, L'\\');
	if (slash_ptr)
		*slash_ptr = 0;

	// If running from 32/64 subfolder, go up one level
	slash_ptr = wcsrchr (dir_buf, L'\\');
	if (slash_ptr)
	{
		LPCWSTR leaf = slash_ptr + 1;
		if (_r_str_compare (leaf, L"32", 2) == 0 || _r_str_compare (leaf, L"64", 2) == 0)
			*slash_ptr = 0;
	}

	return _r_obj_createstring (dir_buf);
}

static PR_STRING _app_get_update_url ()
{
	PR_STRING url = _r_config_getstring (LAUNCHBRO_UPDATE_CONFIG_KEY, NULL);

	if (url)
		return url;

	return _r_obj_createstring (LAUNCHBRO_RELEASE_API);
}

static BOOLEAN _app_is_running ()
{
	HANDLE hmutex;

	hmutex = CreateMutexW (NULL, FALSE, SELF_UPDATE_MUTEX);

	if (!hmutex)
		return TRUE;

	if (GetLastError () == ERROR_ALREADY_EXISTS)
	{
		CloseHandle (hmutex);
		return TRUE;
	}

	CloseHandle (hmutex);
	return FALSE;
}

BOOLEAN _app_check_self_update (
	_In_ HWND hwnd,
	_Out_ PR_STRING* new_version_ptr,
	_Out_ PR_STRING* download_url_ptr
)
{
	R_DOWNLOAD_INFO download_info;
	PR_STRING proxy_string = NULL;
	HINTERNET hsession = NULL;
	PR_STRING json = NULL;
	PR_STRING tag_name = NULL;
	PR_STRING selected_url = NULL;
	PR_STRING api_url = NULL;
	LPCWSTR assets_ptr;
	LPCWSTR scan_ptr;
	BOOLEAN is_success = FALSE;
	NTSTATUS status;

	*new_version_ptr = NULL;
	*download_url_ptr = NULL;

	api_url = _app_get_update_url ();

	if (!api_url)
		return FALSE;

	proxy_string = _r_app_getproxyconfiguration ();
	hsession = _r_inet_createsession (_r_app_getuseragent (), proxy_string);

	if (!hsession)
		goto CleanupExit;

	_r_inet_initializedownload (&download_info, NULL, NULL, NULL);

	{
		R_STRINGREF api_url_sr;
		_r_obj_initializestringref_ex (&api_url_sr, api_url->buffer, api_url->length);
		status = _r_inet_begindownload (hsession, &api_url_sr, &download_info);
	}

	if (status != STATUS_SUCCESS)
		goto CleanupExit;

	if (_r_obj_isstringempty (download_info.string))
		goto CleanupExit;

	_r_obj_movereference ((PVOID_PTR)&json, download_info.string);
	download_info.string = NULL;
	_r_inet_destroydownload (&download_info);

	tag_name = _app_json_find_string_value (json->buffer, L"tag_name");

	if (!tag_name)
		goto CleanupExit;

	if (tag_name->length >= sizeof (WCHAR) && (tag_name->buffer[0] == L'v' || tag_name->buffer[0] == L'V'))
	{
		PR_STRING trimmed = _r_obj_createstring_ex (tag_name->buffer + 1, tag_name->length - sizeof (WCHAR));

		if (trimmed)
		{
			_r_obj_dereference (tag_name);
			tag_name = trimmed;
		}
	}

	assets_ptr = wcsstr (json->buffer, L"\"assets\"");

	if (!assets_ptr)
		goto CleanupExit;

	scan_ptr = assets_ptr;

	{
		LONG best_score = -1;
		LPCWSTR p = scan_ptr;

		while ((p = wcsstr (p, L"\"name\"")) != NULL)
		{
			PR_STRING asset_name = NULL;
			PR_STRING asset_url = NULL;
			LPCWSTR next_name;
			LPCWSTR url_key;
			LPCWSTR url_search_end;
			LONG score = 0;

			asset_name = _app_json_find_string_value (p, L"name");

			if (!asset_name)
			{
				p += 6;
				continue;
			}

			next_name = wcsstr (p + 6, L"\"name\"");
			url_search_end = next_name ? next_name : (json->buffer + (json->length / sizeof (WCHAR)));

			url_key = wcsstr (p, L"\"browser_download_url\"");

			if (!url_key || url_key >= url_search_end)
			{
				_r_obj_dereference (asset_name);
				p += 6;
				continue;
			}

			asset_url = _app_json_find_string_value (url_key, L"browser_download_url");

			if (!asset_url)
			{
				_r_obj_dereference (asset_name);
				p += 6;
				continue;
			}

			if (_app_wcs_endswith (asset_name->buffer, L".zip"))
				score += 10;
			else if (_app_wcs_endswith (asset_name->buffer, L".7z"))
				score += 9;
			else
				score = -1;

			if (score >= 0)
			{
				if (_app_wcs_contains_i (asset_name->buffer, L"portable"))
					score += 2;
			}

			if (score > best_score)
			{
				best_score = score;

				if (selected_url)
					_r_obj_dereference (selected_url);

				selected_url = asset_url;
				asset_url = NULL;
			}

			_r_obj_dereference (asset_name);

			if (asset_url)
				_r_obj_dereference (asset_url);

			p += 6;
		}
	}

	if (!selected_url)
		goto CleanupExit;

	{
		R_STRINGREF current_version_sr;
		_r_obj_initializestringref (&current_version_sr, APP_VERSION);

		if (_r_str_versioncompare (&current_version_sr, &tag_name->sr) == -1)
		{
			_r_obj_movereference ((PVOID_PTR)new_version_ptr, tag_name);
			tag_name = NULL;
			_r_obj_movereference ((PVOID_PTR)download_url_ptr, selected_url);
			selected_url = NULL;
			is_success = TRUE;
		}
	}

CleanupExit:

	if (selected_url)
		_r_obj_dereference (selected_url);

	if (tag_name)
		_r_obj_dereference (tag_name);

	if (json)
		_r_obj_dereference (json);

	if (hsession)
		_r_inet_close (hsession);

	if (proxy_string)
		_r_obj_dereference (proxy_string);

	if (api_url)
		_r_obj_dereference (api_url);

	return is_success;
}

static BOOLEAN NTAPI _app_self_download_callback (
	_In_ ULONG total_written,
	_In_ ULONG total_length,
	_In_ PVOID lparam
)
{
	HWND hwnd = (HWND)lparam;

	if (hwnd)
		_app_setstatus (hwnd, NULL, _r_locale_getstring (IDS_UPDATE_DOWNLOADING), total_written, total_length);

	return TRUE;
}

BOOLEAN _app_download_self_update (
	_In_ HWND hwnd,
	_In_ PR_STRING download_url,
	_Out_ PR_STRING* out_path_ptr
)
{
	R_DOWNLOAD_INFO download_info;
	PR_STRING proxy_string;
	PR_STRING temp_file;
	PR_STRING temp_dir;
	HINTERNET hsession;
	HANDLE hfile;
	BOOLEAN is_success = FALSE;
	NTSTATUS status;
	INT retry_count = 0;
	const INT max_retries = 3;

	*out_path_ptr = NULL;

	temp_dir = _r_sys_gettempdirectory ();

	if (!temp_dir)
		return FALSE;

	temp_file = _r_format_string (L"%s\\launchbro_update_%.0f.zip", temp_dir->buffer, (double)GetTickCount64 ());

	_r_obj_dereference (temp_dir);

	if (!temp_file)
		return FALSE;

	proxy_string = _r_app_getproxyconfiguration ();

	hsession = _r_inet_createsession (_r_app_getuseragent (), proxy_string);

	if (hsession)
	{
		for (retry_count = 0; retry_count <= max_retries; retry_count++)
		{
			status = _r_fs_createfile (
				&temp_file->sr,
				FILE_OVERWRITE_IF,
				FILE_GENERIC_WRITE,
				FILE_SHARE_READ,
				FILE_ATTRIBUTE_NORMAL,
				FILE_SEQUENTIAL_ONLY,
				FALSE,
				NULL,
				&hfile
			);

			if (!NT_SUCCESS (status))
				break;

			_r_inet_initializedownload (&download_info, hfile, &_app_self_download_callback, hwnd);

			status = _r_inet_begindownload (hsession, &download_url->sr, &download_info);

			_r_inet_destroydownload (&download_info);

			if (status == STATUS_SUCCESS)
			{
				is_success = TRUE;
				*out_path_ptr = temp_file;
				break;
			}

			_r_fs_deletefile (&temp_file->sr, NULL);

			if (retry_count < max_retries)
			{
				ULONG delay_ms = 1000 * (1 << retry_count);
				Sleep (delay_ms);
			}
		}

		_r_inet_close (hsession);
	}

	if (proxy_string)
		_r_obj_dereference (proxy_string);

	if (!is_success && temp_file)
		_r_obj_dereference (temp_file);

	return is_success;
}

static BOOLEAN _app_create_helper_bat (
	_In_ PR_STRING zip_path,
	_In_ PR_STRING app_root,
	_In_ BOOLEAN is_migration,
	_In_opt_ PR_STRING old_folder
)
{
	PR_STRING bat_path;
	PR_STRING bat_content;
	HANDLE hfile;
	ULONG written;
	NTSTATUS status;
	BOOLEAN is_success = FALSE;

	bat_path = _r_format_string (L"%s\\_launchbro_update.bat", app_root->buffer);

	if (!bat_path)
		return FALSE;

	bat_content = _r_format_string (
		L"@echo off\r\n"
		L"timeout /t 3 /nobreak >nul\r\n"
		L":wait_loop\r\n"
		L"tasklist | findstr /i \"launchbro.exe\" >nul\r\n"
		L"if %%errorlevel%% == 0 (\r\n"
		L"  timeout /t 1 /nobreak >nul\r\n"
		L"  goto wait_loop\r\n"
		L")\r\n"
		L"\r\n"
		L"set \"ZIP=%s\"\r\n"
		L"set \"ROOT=%s\"\r\n"
		L"set \"TEMP_EXTRACT=%s\\_launchbro_new\"\r\n"
		L"\r\n"
		L"if exist \"%%TEMP_EXTRACT%%\" rmdir /s /q \"%%TEMP_EXTRACT%%\"\r\n"
		L"powershell -NoProfile -Command \"Expand-Archive -Path '%s' -DestinationPath '%%TEMP_EXTRACT%%' -Force\"\r\n"
		L"if errorlevel 1 goto cleanup\r\n"
		L"\r\n"
		L"if exist \"%%TEMP_EXTRACT%%\\launchbro\" (\r\n"
		L"  xcopy /e /y /i \"%%TEMP_EXTRACT%%\\launchbro\\32\\*\" \"%%ROOT%%\\32\\\" >nul 2>&1\r\n"
		L"  xcopy /e /y /i \"%%TEMP_EXTRACT%%\\launchbro\\64\\*\" \"%%ROOT%%\\64\\\" >nul 2>&1\r\n"
		L"  xcopy /e /y /i \"%%TEMP_EXTRACT%%\\launchbro\\i18n\" \"%%ROOT%%\\i18n\\\" >nul 2>&1\r\n"
		L"  copy /y \"%%TEMP_EXTRACT%%\\launchbro\\*.ini\" \"%%ROOT%%\\\" >nul 2>&1\r\n"
		L"  copy /y \"%%TEMP_EXTRACT%%\\launchbro\\*.txt\" \"%%ROOT%%\\\" >nul 2>&1\r\n"
		L"  copy /y \"%%TEMP_EXTRACT%%\\launchbro\\*.lng\" \"%%ROOT%%\\\" >nul 2>&1\r\n"
		L") else if exist \"%%TEMP_EXTRACT%%\\32\" (\r\n"
		L"  xcopy /e /y /i \"%%TEMP_EXTRACT%%\\32\\*\" \"%%ROOT%%\\32\\\" >nul 2>&1\r\n"
		L"  xcopy /e /y /i \"%%TEMP_EXTRACT%%\\64\\*\" \"%%ROOT%%\\64\\\" >nul 2>&1\r\n"
		L"  xcopy /e /y /i \"%%TEMP_EXTRACT%%\\i18n\" \"%%ROOT%%\\i18n\\\" >nul 2>&1\r\n"
		L"  copy /y \"%%TEMP_EXTRACT%%\\*.ini\" \"%%ROOT%%\\\" >nul 2>&1\r\n"
		L"  copy /y \"%%TEMP_EXTRACT%%\\*.txt\" \"%%ROOT%%\\\" >nul 2>&1\r\n"
		L"  copy /y \"%%TEMP_EXTRACT%%\\*.lng\" \"%%ROOT%%\\\" >nul 2>&1\r\n"
		L")\r\n"
		L"\r\n"
		L":cleanup\r\n"
		L"if exist \"%%TEMP_EXTRACT%%\" rmdir /s /q \"%%TEMP_EXTRACT%%\"\r\n"
		L"del /f /q \"%%ZIP%%\" >nul 2>&1\r\n"
		L"start \"\" \"%%ROOT%%\\64\\launchbro.exe\"\r\n"
		L"del /f /q \"%s\"\r\n",
		zip_path->buffer,
		app_root->buffer,
		app_root->buffer,
		zip_path->buffer,
		bat_path->buffer
	);

	if (!bat_content)
		goto CleanupExit;

	status = _r_fs_createfile (
		&bat_path->sr,
		FILE_OVERWRITE_IF,
		FILE_GENERIC_WRITE,
		FILE_SHARE_READ,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SEQUENTIAL_ONLY,
		FALSE,
		NULL,
		&hfile
	);

	if (!NT_SUCCESS (status))
		goto CleanupExit;

	if (WriteFile (hfile, bat_content->buffer, (ULONG)bat_content->length, &written, NULL))
		is_success = TRUE;

	CloseHandle (hfile);

CleanupExit:

	if (bat_content)
		_r_obj_dereference (bat_content);

	if (bat_path)
		_r_obj_dereference (bat_path);

	return is_success;
}

VOID _app_apply_self_update (
	_In_ HWND hwnd,
	_In_ PR_STRING zip_path
)
{
	PR_STRING app_root;
	PR_STRING bat_path;
	SHELLEXECUTEINFOW sei = {0};

	app_root = _app_get_app_root_dir ();

	if (!app_root)
		return;

	if (!_app_create_helper_bat (zip_path, app_root, FALSE, NULL))
	{
		_r_obj_dereference (app_root);
		return;
	}

	bat_path = _r_format_string (L"%s\\_launchbro_update.bat", app_root->buffer);

	_r_obj_dereference (app_root);

	if (!bat_path)
		return;

	sei.cbSize = sizeof (sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
	sei.lpVerb = L"open";
	sei.lpFile = bat_path->buffer;
	sei.nShow = SW_HIDE;

	if (ShellExecuteExW (&sei))
	{
		_r_tray_popup (hwnd, &GUID_TrayIcon, NIIF_INFO, _r_app_getname (), _r_locale_getstring (IDS_UPDATE_DONE));
		_r_wnd_sendmessage (hwnd, 0, WM_DESTROY, 0, 0);
	}

	_r_obj_dereference (bat_path);
}

BOOLEAN _app_check_migration ()
{
	PR_STRING app_root;
	WCHAR parent_buf[4096] = {0};
	PR_STRING chrlauncher_path;
	BOOLEAN found = FALSE;
	PWSTR slash_ptr;

	app_root = _app_get_app_root_dir ();

	if (!app_root)
		return FALSE;

	_r_str_copy (parent_buf, RTL_NUMBER_OF (parent_buf), app_root->buffer);

	slash_ptr = wcsrchr (parent_buf, L'\\');
	if (slash_ptr)
		*slash_ptr = 0;

	chrlauncher_path = _r_format_string (L"%s\\chrlauncher", parent_buf);

	if (chrlauncher_path)
	{
		if (_r_fs_exists (&chrlauncher_path->sr))
			found = TRUE;

		_r_obj_dereference (chrlauncher_path);
	}

	_r_obj_dereference (app_root);

	return found;
}

VOID _app_perform_migration (
	_In_ HWND hwnd
)
{
	PR_STRING app_root;
	WCHAR parent_buf[4096] = {0};
	PR_STRING bat_path = NULL;
	PR_STRING bat_content = NULL;
	HANDLE hfile;
	ULONG written;
	NTSTATUS status;
	PWSTR slash_ptr;
	SHELLEXECUTEINFOW sei = {0};

	app_root = _app_get_app_root_dir ();

	if (!app_root)
		return;

	_r_str_copy (parent_buf, RTL_NUMBER_OF (parent_buf), app_root->buffer);

	slash_ptr = wcsrchr (parent_buf, L'\\');
	if (slash_ptr)
		*slash_ptr = 0;

	bat_path = _r_format_string (L"%s\\_launchbro_migrate.bat", parent_buf);

	if (!bat_path)
		goto CleanupExit;

	bat_content = _r_format_string (
		L"@echo off\r\n"
		L"timeout /t 2 /nobreak >nul\r\n"
		L":wait_loop\r\n"
		L"tasklist | findstr /i \"launchbro.exe\" >nul\r\n"
		L"if %%errorlevel%% == 0 (\r\n"
		L"  timeout /t 1 /nobreak >nul\r\n"
		L"  goto wait_loop\r\n"
		L")\r\n"
		L"\r\n"
		L"set \"SRC=%s\\chrlauncher\"\r\n"
		L"set \"DST=%s\\launchbro\"\r\n"
		L"\r\n"
		L"if exist \"%%DST%%\" rmdir /s /q \"%%DST%%\"\r\n"
		L"rename \"%%SRC%%\" \"launchbro\"\r\n"
		L"start \"\" \"%%DST%%\\64\\launchbro.exe\"\r\n"
		L"del /f /q \"%s\"\r\n",
		parent_buf,
		parent_buf,
		bat_path->buffer
	);

	if (!bat_content)
		goto CleanupExit;

	status = _r_fs_createfile (
		&bat_path->sr,
		FILE_OVERWRITE_IF,
		FILE_GENERIC_WRITE,
		FILE_SHARE_READ,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SEQUENTIAL_ONLY,
		FALSE,
		NULL,
		&hfile
	);

	if (!NT_SUCCESS (status))
		goto CleanupExit;

	WriteFile (hfile, bat_content->buffer, (ULONG)bat_content->length, &written, NULL);
	CloseHandle (hfile);

	sei.cbSize = sizeof (sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
	sei.lpVerb = L"open";
	sei.lpFile = bat_path->buffer;
	sei.nShow = SW_HIDE;

	if (ShellExecuteExW (&sei))
		_r_wnd_sendmessage (hwnd, 0, WM_DESTROY, 0, 0);

CleanupExit:

	if (bat_content)
		_r_obj_dereference (bat_content);

	if (bat_path)
		_r_obj_dereference (bat_path);

	if (app_root)
		_r_obj_dereference (app_root);
}

