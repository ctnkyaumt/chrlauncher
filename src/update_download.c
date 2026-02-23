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
)
{
	PR_STRING lastcheck_key = NULL;

	if (pbi && pbi->instance_id > 1)
		lastcheck_key = _r_format_string (L"ChromiumLastCheck%" TEXT (PR_LONG), pbi->instance_id);

	_r_config_setlong64 (lastcheck_key ? lastcheck_key->buffer : L"ChromiumLastCheck", _r_unixtime_now ());

	if (lastcheck_key)
		_r_obj_dereference (lastcheck_key);
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
		R_STRINGREF cromite_type = PR_STRINGREF_INIT (L"cromite");

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
