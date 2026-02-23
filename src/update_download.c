// chrlauncher
// Copyright (c) 2015-2025 Henry++

#include "routine.h"

#include "main.h"
#include "rapp.h"

#include "resource.h"

extern R_QUEUED_LOCK lock_download;

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

VOID _app_set_lastcheck (
	_In_ PBROWSER_INFORMATION pbi
);

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

static BOOLEAN _app_checkupdate_github_latest_release (
	_In_ HWND hwnd,
	_In_ PBROWSER_INFORMATION pbi,
	_In_ LPCWSTR release_api_url,
	_In_ BOOLEAN is_exists,
	_Out_ PBOOLEAN is_error_ptr
)
{
	R_DOWNLOAD_INFO download_info;
	PR_STRING proxy_string = NULL;
	HINTERNET hsession = NULL;
	PR_STRING json = NULL;
	PR_STRING tag_name = NULL;
	PR_STRING selected_url = NULL;
	LPCWSTR assets_ptr;
	LPCWSTR scan_ptr;
	BOOLEAN is_success = FALSE;
	NTSTATUS status;

	*is_error_ptr = FALSE;

	proxy_string = _r_app_getproxyconfiguration ();
	hsession = _r_inet_createsession (_r_app_getuseragent (), proxy_string);

	if (!hsession)
	{
		*is_error_ptr = TRUE;
		goto CleanupExit;
	}

	_r_inet_initializedownload (&download_info, NULL, NULL, NULL);

	{
		R_STRINGREF api_url_sr;
		_r_obj_initializestringref_ex (&api_url_sr, (LPWSTR)release_api_url, wcslen (release_api_url) * sizeof (WCHAR));
		status = _r_inet_begindownload (hsession, &api_url_sr, &download_info);
	}

	if (status != STATUS_SUCCESS)
	{
		_r_show_errormessage (hwnd, NULL, status, L"Could not download update.", ET_WINHTTP);
		*is_error_ptr = TRUE;
		_r_inet_destroydownload (&download_info);
		goto CleanupExit;
	}

	if (_r_obj_isstringempty (download_info.string))
	{
		_r_show_message (hwnd, MB_OK | MB_ICONSTOP, NULL, L"Configuration was not found.");
		*is_error_ptr = TRUE;
		_r_inet_destroydownload (&download_info);
		goto CleanupExit;
	}

	_r_obj_movereference ((PVOID_PTR)&json, download_info.string);
	download_info.string = NULL;
	_r_inet_destroydownload (&download_info);

	tag_name = _app_json_find_string_value (json->buffer, L"tag_name");

	if (!tag_name)
	{
		_r_show_message (hwnd, MB_OK | MB_ICONSTOP, NULL, L"Configuration was not found.");
		*is_error_ptr = TRUE;
		goto CleanupExit;
	}

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
	{
		_r_show_message (hwnd, MB_OK | MB_ICONSTOP, NULL, L"Configuration was not found.");
		*is_error_ptr = TRUE;
		goto CleanupExit;
	}

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
				if (pbi->architecture == 64)
				{
					if (_app_wcs_contains_i (asset_name->buffer, L"win64") || _app_wcs_contains_i (asset_name->buffer, L"x64"))
						score += 6;
					else if (_app_wcs_contains_i (asset_name->buffer, L"64"))
						score += 5;
				}
				else if (pbi->architecture == 32)
				{
					if (_app_wcs_contains_i (asset_name->buffer, L"win32") || _app_wcs_contains_i (asset_name->buffer, L"x86"))
						score += 6;
					else if (_app_wcs_contains_i (asset_name->buffer, L"32"))
						score += 5;
				}

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
	{
		_r_show_message (hwnd, MB_OK | MB_ICONSTOP, NULL, L"Configuration was not found.");
		*is_error_ptr = TRUE;
		goto CleanupExit;
	}

	_r_obj_movereference ((PVOID_PTR)&pbi->download_url, selected_url);
	selected_url = NULL;

	_r_obj_movereference ((PVOID_PTR)&pbi->new_version, tag_name);
	tag_name = NULL;

	_app_update_browser_info (hwnd, pbi);

	if (pbi->new_version && pbi->current_version)
		is_success = (_r_str_versioncompare (&pbi->current_version->sr, &pbi->new_version->sr) == -1);
	else
		is_success = TRUE;

	if (is_exists && !is_success)
	{
		SAFE_DELETE_REFERENCE (pbi->download_url);
		_app_set_lastcheck (pbi);
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

	return is_success;
}

BOOLEAN _app_checkupdate (
	_In_ HWND hwnd,
	_In_ PBROWSER_INFORMATION pbi,
	_Out_ PBOOLEAN is_error_ptr
)
{
	PR_HASHTABLE hashtable = NULL;
	R_DOWNLOAD_INFO download_info;
	PR_STRING update_url;
	PR_STRING url;
	HINTERNET hsession;
	PR_STRING string;
	PR_STRING proxy_string;
	BOOLEAN is_updaterequired;
	BOOLEAN is_newversion = FALSE;
	BOOLEAN is_success = FALSE;
	BOOLEAN is_exists;
	NTSTATUS status;

	*is_error_ptr = FALSE;

	if (_app_ishaveupdate (pbi))
		return TRUE;

	is_exists = _r_fs_exists (&pbi->binary_path->sr);
	is_updaterequired = _app_isupdaterequired (pbi);

	_app_setstatus (hwnd, pbi->htaskbar, _r_locale_getstring (IDS_STATUS_CHECK), 0, 0);

	SAFE_DELETE_REFERENCE (pbi->new_version);
	pbi->timestamp = 0;

	_app_update_browser_info (hwnd, pbi);

	if (!is_exists || is_updaterequired)
	{
		R_STRINGREF r3dfox_type = PR_STRINGREF_INIT (L"r3dfox");
		R_STRINGREF iceweasel_type = PR_STRINGREF_INIT (L"iceweasel");
		R_STRINGREF cromite_type = PR_STRINGREF_INIT (L"cromite");

		if (pbi->browser_type && _r_str_isequal (&pbi->browser_type->sr, &r3dfox_type, TRUE))
			return _app_checkupdate_github_latest_release (hwnd, pbi, L"https://api.github.com/repos/Eclipse-Community/r3dfox/releases/latest", is_exists, is_error_ptr);

		if (pbi->browser_type && _r_str_isequal (&pbi->browser_type->sr, &iceweasel_type, TRUE))
			return _app_checkupdate_github_latest_release (hwnd, pbi, L"https://api.github.com/repos/adonais/iceweasel/releases/latest", is_exists, is_error_ptr);

		update_url = _r_config_getstring (L"ChromiumUpdateUrl", NULL);

		if (!update_url)
		{
			if (pbi->browser_type && _r_str_isequal (&pbi->browser_type->sr, &cromite_type, TRUE))
				update_url = _r_obj_createstring (CHROMIUM_UPDATE_URL_CROMITE);
			else
				update_url = _r_obj_createstring (CHROMIUM_UPDATE_URL);
		}

		if (!update_url)
			return FALSE;

		url = _r_format_string (update_url->buffer, pbi->architecture, pbi->browser_type->buffer);

		if (url)
		{
			proxy_string = _r_app_getproxyconfiguration ();

			hsession = _r_inet_createsession (_r_app_getuseragent (), proxy_string);

			if (hsession)
			{
				_r_inet_initializedownload (&download_info, NULL, NULL, NULL);

				status = _r_inet_begindownload (hsession, &url->sr, &download_info);

				if (status == STATUS_SUCCESS)
				{
					string = download_info.string;

					if (_r_obj_isstringempty (string))
					{
						_r_show_message (hwnd, MB_OK | MB_ICONSTOP, NULL, L"Configuration was not found.");

						*is_error_ptr = TRUE;
					}
					else
					{
						hashtable = _r_str_unserialize (&string->sr, L';', L'=');
					}
				}
				else
				{
					_r_show_errormessage (hwnd, NULL, status, L"Could not download update.", ET_WINHTTP);

					*is_error_ptr = TRUE;
				}

				_r_inet_destroydownload (&download_info);

				_r_inet_close (hsession);
			}

			if (proxy_string)
				_r_obj_dereference (proxy_string);

			_r_obj_dereference (url);
		}

		_r_obj_dereference (update_url);
	}

	if (hashtable)
	{
		R_STRINGREF download_key = PR_STRINGREF_INIT (L"download");
		R_STRINGREF version_key = PR_STRINGREF_INIT (L"version");
		R_STRINGREF timestamp_key = PR_STRINGREF_INIT (L"timestamp");

		string = _r_obj_findhashtablepointer (hashtable, _r_str_gethash2 (&download_key, TRUE));

		_r_obj_movereference ((PVOID_PTR)&pbi->download_url, string);

		string = _r_obj_findhashtablepointer (hashtable, _r_str_gethash2 (&version_key, TRUE));

		_r_obj_movereference ((PVOID_PTR)&pbi->new_version, string);

		string = _r_obj_findhashtablepointer (hashtable, _r_str_gethash2 (&timestamp_key, TRUE));

		if (string)
		{
			pbi->timestamp = _r_str_tolong64 (&string->sr);

			_r_obj_dereference (string);
		}

		_app_update_browser_info (hwnd, pbi);

		if (pbi->new_version && pbi->current_version)
			is_newversion = (_r_str_versioncompare (&pbi->current_version->sr, &pbi->new_version->sr) == -1);

		if (!is_exists || is_newversion)
		{
			is_success = TRUE;
		}
		else
		{
			SAFE_DELETE_REFERENCE (pbi->download_url);

			_app_set_lastcheck (pbi);
		}

		_r_obj_dereference (hashtable);
	}

	_app_setstatus (hwnd, pbi->htaskbar, NULL, 0, 0);

	return is_success;
}

BOOLEAN NTAPI _app_downloadupdate_callback (
	_In_ ULONG total_written,
	_In_ ULONG total_length,
	_In_ PVOID lparam
)
{
	PBROWSER_INFORMATION pbi;

	pbi = lparam;

	_app_setstatus (pbi->hwnd, pbi->htaskbar, _r_locale_getstring (IDS_STATUS_DOWNLOAD), total_written, total_length);

	return TRUE;
}

BOOLEAN _app_downloadupdate (
	_In_ HWND hwnd,
	_In_ PBROWSER_INFORMATION pbi,
	_Out_ PBOOLEAN is_error_ptr
)
{
	R_DOWNLOAD_INFO download_info;
	PR_STRING proxy_string;
	PR_STRING temp_file;
	HINTERNET hsession;
	HANDLE hfile;
	BOOLEAN is_success = FALSE;
	NTSTATUS status;

	*is_error_ptr = FALSE;

	if (_app_isupdatedownloaded (pbi))
		return TRUE;

	temp_file = _r_obj_concatstrings (
		2,
		pbi->cache_path->buffer,
		L".tmp"
	);

	_r_fs_deletefile (&pbi->cache_path->sr, NULL);

	_app_setstatus (hwnd, pbi->htaskbar, _r_locale_getstring (IDS_STATUS_DOWNLOAD), 0, 1);

	_r_queuedlock_acquireshared (&lock_download);

	proxy_string = _r_app_getproxyconfiguration ();

	hsession = _r_inet_createsession (_r_app_getuseragent (), proxy_string);

	if (hsession)
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
		{
			_r_show_errormessage (hwnd, NULL, status, temp_file->buffer, ET_NATIVE);

			*is_error_ptr = TRUE;
		}
		else
		{
			_r_inet_initializedownload (&download_info, hfile, &_app_downloadupdate_callback, pbi);

			status = _r_inet_begindownload (hsession, &pbi->download_url->sr, &download_info);

			_r_inet_destroydownload (&download_info);

			if (status != STATUS_SUCCESS)
			{
				_r_show_errormessage (hwnd, NULL, status, pbi->download_url->buffer, ET_WINHTTP);

				_r_fs_deletefile (&pbi->cache_path->sr, NULL);

				*is_error_ptr = TRUE;
			}
			else
			{
				SAFE_DELETE_REFERENCE (pbi->download_url);

				_r_fs_movefile (&temp_file->sr, &pbi->cache_path->sr, FALSE);

				is_success = TRUE;
			}

			_r_fs_deletefile (&temp_file->sr, NULL);
		}

		_r_inet_close (hsession);
	}

	_r_queuedlock_releaseshared (&lock_download);

	if (proxy_string)
		_r_obj_dereference (proxy_string);

	_r_obj_dereference (temp_file);

	_app_setstatus (hwnd, pbi->htaskbar, NULL, 0, 0);

	return is_success;
}
