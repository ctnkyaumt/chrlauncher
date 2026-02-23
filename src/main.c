// chrlauncher
// Copyright (c) 2015-2025 Henry++

#include "routine.h"

#include "main.h"
#include "rapp.h"

#include "CpuArch.h"

#include "7z.h"
#include "7zAlloc.h"
#include "7zBuf.h"
#include "7zCrc.h"
#include "7zFile.h"
#include "7zWindows.h"

#include "miniz.h"

#include "resource.h"

#include <shlobj.h>
#include <shobjidl.h>

BROWSER_INFORMATION browser_info = {0};

R_QUEUED_LOCK lock_download = PR_QUEUED_LOCK_INIT;
R_QUEUED_LOCK lock_thread = PR_QUEUED_LOCK_INIT;

R_WORKQUEUE workqueue;

static VOID _app_set_lastcheck (
	_In_ PBROWSER_INFORMATION pbi
);

static VOID _app_create_profileshortcut (
	_In_ PBROWSER_INFORMATION pbi
)
{
	WCHAR exe_path[4096] = {0};
	PWSTR desktop_path = NULL;
	PR_STRING link_title = NULL;
	PR_STRING link_path = NULL;
	HRESULT hr_init;
	HRESULT hr;
	IShellLinkW *psl = NULL;
	IPersistFile *ppf = NULL;

	if (!pbi || _r_obj_isstringempty (pbi->profile_dir))
		return;

	if (!_r_fs_exists (&pbi->profile_dir->sr))
	{
		PR_STRING base_dir;

		base_dir = _r_path_getbasedirectory (&pbi->profile_dir->sr);

		if (base_dir)
		{
			if (!_r_fs_exists (&base_dir->sr))
				_r_fs_createdirectory (&base_dir->sr);

			_r_obj_dereference (base_dir);
		}

		_r_fs_createdirectory (&pbi->profile_dir->sr);
	}

	if (!GetModuleFileNameW (NULL, exe_path, RTL_NUMBER_OF (exe_path)))
		return;

	hr = SHGetKnownFolderPath (&FOLDERID_Desktop, KF_FLAG_DEFAULT, NULL, &desktop_path);

	if (FAILED (hr) || !desktop_path)
		return;

	if (pbi->instance_id <= 1)
		link_title = _r_format_string (L"%s profile (%" TEXT (PR_LONG) L"-bit)", _r_app_getname (), pbi->architecture);
	else
		link_title = _r_format_string (L"%s profile (%" TEXT (PR_LONG) L"-bit) #%" TEXT (PR_LONG), _r_app_getname (), pbi->architecture, pbi->instance_id);

	if (!link_title)
	{
		CoTaskMemFree (desktop_path);
		return;
	}

	link_path = _r_format_string (L"%s\\%s.lnk", desktop_path, link_title->buffer);

	CoTaskMemFree (desktop_path);

	if (!link_path)
	{
		_r_obj_dereference (link_title);
		return;
	}

	if (_r_fs_exists (&link_path->sr))
	{
		_r_obj_dereference (link_title);
		_r_obj_dereference (link_path);
		return;
	}

	hr_init = CoInitializeEx (NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

	hr = CoCreateInstance (&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (PVOID_PTR)&psl);

	if (SUCCEEDED (hr) && psl)
	{
		psl->lpVtbl->SetPath (psl, pbi->profile_dir->buffer);
		psl->lpVtbl->SetWorkingDirectory (psl, pbi->profile_dir->buffer);
		psl->lpVtbl->SetIconLocation (psl, exe_path, 0);
		psl->lpVtbl->SetDescription (psl, link_title->buffer);

		hr = psl->lpVtbl->QueryInterface (psl, &IID_IPersistFile, (PVOID_PTR)&ppf);

		if (SUCCEEDED (hr) && ppf)
		{
			ppf->lpVtbl->Save (ppf, link_path->buffer, TRUE);
			ppf->lpVtbl->Release (ppf);
		}

		psl->lpVtbl->Release (psl);
	}

	if (SUCCEEDED (hr_init))
		CoUninitialize ();

	_r_obj_dereference (link_title);
	_r_obj_dereference (link_path);
}

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

		_app_update_browser_info (hwnd, &browser_info);

		if (pbi->new_version && pbi->current_version)
			is_newversion = (_r_str_versioncompare (&pbi->current_version->sr, &pbi->new_version->sr) == -1);

		if (!is_exists || is_newversion)
		{
			is_success = TRUE;
		}
		else
		{
			SAFE_DELETE_REFERENCE (pbi->download_url); // clear download url if update was not found

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

			_r_inet_destroydownload (&download_info); // required!

			if (status != STATUS_SUCCESS)
			{
				_r_show_errormessage (hwnd, NULL, status, pbi->download_url->buffer, ET_WINHTTP);

				_r_fs_deletefile (&pbi->cache_path->sr, NULL);

				*is_error_ptr = TRUE;
			}
			else
			{
				SAFE_DELETE_REFERENCE (pbi->download_url); // clear download url

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

SRes _app_unpack_7zip (
	_In_ HWND hwnd,
	_In_ PBROWSER_INFORMATION pbi,
	_In_ PR_STRINGREF bin_name
)
{
#define kInputBufSize ((ULONG_PTR)1 << 18)

	static R_STRINGREF separator_sr = PR_STRINGREF_INIT (L"\\");
	static const ISzAlloc g_Alloc = {SzAlloc, SzFree};

	ISzAlloc alloc_imp = g_Alloc;
	ISzAlloc alloc_temp_imp = g_Alloc;
	CFileInStream archive_stream = {0};
	CLookToRead2 look_stream;
	CSzArEx db;
	ULONG_PTR temp_size = 0;
	LPWSTR temp_buff = NULL;

	// if you need cache, use these 3 variables.
	// if you use external function, you can make these variable as static.

	UInt32 block_index = UINT32_MAX; // it can have any value before first call (if out_buffer = 0)
	Byte *out_buffer = NULL; // it must be 0 before first call for each new archive.
	ULONG_PTR out_buffer_size = 0; // it can have any value before first call (if out_buffer = 0)
	R_STRINGREF path;
	PR_STRING root_dir_name = NULL;
	PR_STRING dest_path;
	PR_STRING sub_dir;
	CSzFile out_file;
	ULONG_PTR offset;
	ULONG_PTR out_size_processed;
	UInt32 attrib;
	UInt64 total_size = 0;
	UInt64 total_read = 0;
	ULONG_PTR processed_size;
	ULONG_PTR length;
	BOOLEAN is_success = FALSE;
	LONG status;

	status = InFile_OpenW (&archive_stream.file, pbi->cache_path->buffer);

	if (status != ERROR_SUCCESS)
	{
		_r_show_errormessage (hwnd, NULL, status, pbi->cache_path->buffer, ET_WINDOWS);

		return status;
	}

	FileInStream_CreateVTable (&archive_stream);
	LookToRead2_CreateVTable (&look_stream, 0);

	SzArEx_Init (&db);

	look_stream.buf = (PUCHAR)ISzAlloc_Alloc (&alloc_imp, kInputBufSize);

	if (!look_stream.buf)
	{
		_r_show_errormessage (hwnd, NULL, STATUS_NO_MEMORY, L"ISzAlloc_Alloc", ET_NATIVE);

		goto CleanupExit;
	}

	look_stream.bufSize = kInputBufSize;
	look_stream.realStream = &archive_stream.vt;

	LookToRead2_INIT (&look_stream);

	CrcGenerateTable ();

	status = SzArEx_Open (&db, &look_stream.vt, &alloc_imp, &alloc_temp_imp);

	if (status != SZ_OK)
	{
		_r_show_errormessage (hwnd, NULL, status, L"SzArEx_Open", ET_NONE);

		goto CleanupExit;
	}

	// find root directory which contains main executable
	for (ULONG_PTR i = 0; i < db.NumFiles; i++)
	{
		if (SzArEx_IsDir (&db, i))
			continue;

		length = SzArEx_GetFileNameUtf16 (&db, i, NULL);
		total_size += SzArEx_GetFileSize (&db, i);

		if (length > temp_size)
		{
			temp_size = length;

			if (temp_buff)
			{
				temp_buff = _r_mem_reallocate (temp_buff, temp_size * sizeof (UInt16));
			}
			else
			{
				temp_buff = _r_mem_allocate (temp_size * sizeof (UInt16));
			}
		}

		if (!root_dir_name)
		{
			length = SzArEx_GetFileNameUtf16 (&db, i, temp_buff);

			if (!length)
				continue;

			_r_obj_initializestringref_ex (&path, temp_buff, (length - 1) * sizeof (WCHAR));

			_r_str_replacechar (&path, OBJ_NAME_ALTPATH_SEPARATOR, OBJ_NAME_PATH_SEPARATOR);

			length = path.length - bin_name->length - separator_sr.length;

			if (_r_str_isendsswith (&path, bin_name, TRUE) && path.buffer[length / sizeof (WCHAR)] == OBJ_NAME_PATH_SEPARATOR)
			{
				_r_obj_movereference ((PVOID_PTR)&root_dir_name, _r_obj_createstring_ex (path.buffer, path.length - bin_name->length));

				_r_str_trimstring (&root_dir_name->sr, &separator_sr, 0);
			}
		}
	}

	for (ULONG_PTR i = 0; i < db.NumFiles; i++)
	{
		length = SzArEx_GetFileNameUtf16 (&db, i, temp_buff);

		if (!length)
			continue;

		_r_obj_initializestringref_ex (&path, temp_buff, (length - 1) * sizeof (WCHAR));

		_r_str_replacechar (&path, OBJ_NAME_ALTPATH_SEPARATOR, OBJ_NAME_PATH_SEPARATOR);

		_r_str_trimstring (&path, &separator_sr, 0);

		// skip non-root dirs
		if (!_r_obj_isstringempty (root_dir_name) && (path.length <= root_dir_name->length || !_r_str_isstartswith (&path, &root_dir_name->sr, TRUE)))
			continue;

		if (root_dir_name)
			_r_str_skiplength (&path, root_dir_name->length + separator_sr.length);

		dest_path = _r_obj_concatstringrefs (
			3,
			&pbi->binary_dir->sr,
			&separator_sr,
			&path
		);

		if (SzArEx_IsDir (&db, i))
		{
			_r_fs_createdirectory (&dest_path->sr);
		}
		else
		{
			total_read += SzArEx_GetFileSize (&db, i);

			_app_setstatus (hwnd, pbi->htaskbar, _r_locale_getstring (IDS_STATUS_INSTALL), total_read, total_size);

			// create directory if not-exist
			sub_dir = _r_path_getbasedirectory (&dest_path->sr);

			if (sub_dir)
			{
				if (!_r_fs_exists (&sub_dir->sr))
					_r_fs_createdirectory (&sub_dir->sr);

				_r_obj_dereference (sub_dir);
			}

			offset = 0;
			out_size_processed = 0;

			status = SzArEx_Extract (&db, &look_stream.vt, (UINT)i, &block_index, &out_buffer, &out_buffer_size, &offset, &out_size_processed, &alloc_imp, &alloc_temp_imp);

			if (status != SZ_OK)
			{
				_r_show_errormessage (hwnd, NULL, status, L"SzArEx_Extract", ET_NONE);
			}
			else
			{
				status = OutFile_OpenW (&out_file, dest_path->buffer);

				if (status != SZ_OK)
				{
					if (status != SZ_ERROR_CRC)
						_r_show_errormessage (hwnd, NULL, status, L"OutFile_OpenW", ET_NONE);
				}
				else
				{
					processed_size = out_size_processed;

					status = File_Write (&out_file, out_buffer + offset, &processed_size);

					if (status != SZ_OK || processed_size != out_size_processed)
					{
						_r_show_errormessage (hwnd, NULL, status, L"File_Write", ET_NONE);
					}
					else
					{
						if (SzBitWithVals_Check (&db.Attribs, i))
						{
							attrib = db.Attribs.Vals[i];

							//	p7zip stores posix attributes in high 16 bits and adds 0x8000 as marker.
							//	We remove posix bits, if we detect posix mode field
							if ((attrib & 0xF0000000) != 0)
								attrib &= 0x7FFF;

							_r_fs_setattributes (dest_path->buffer, NULL, attrib);
						}
					}

					File_Close (&out_file);
				}
			}
		}

		_r_obj_dereference (dest_path);

		if (!is_success)
			is_success = TRUE;
	}

CleanupExit:

	if (root_dir_name)
		_r_obj_dereference (root_dir_name);

	if (out_buffer)
		ISzAlloc_Free (&alloc_imp, out_buffer);

	if (look_stream.buf)
		ISzAlloc_Free (&alloc_imp, look_stream.buf);

	if (temp_buff)
		_r_mem_free (temp_buff);

	SzArEx_Free (&db, &alloc_imp);

	File_Close (&archive_stream.file);

	return status;
}

BOOLEAN _app_unpack_zip (
	_In_ HWND hwnd,
	_In_ PBROWSER_INFORMATION pbi,
	_In_ PR_STRINGREF bin_name
)
{
	static R_STRINGREF separator_sr = PR_STRINGREF_INIT (L"\\");

	mz_zip_archive_file_stat file_stat;
	mz_zip_archive zip_archive = {0};
	PR_STRING root_dir_name = NULL;
	R_BYTEREF path_sr;
	PR_STRING path;
	PR_STRING dest_path;
	PR_STRING sub_dir;
	ULONG64 total_size = 0;
	ULONG64 total_read = 0; // this is our progress so far
	ULONG_PTR length;
	UINT total_files;
	BOOLEAN is_success = FALSE;
	NTSTATUS status;

	if (!mz_zip_reader_init_file_v2 (&zip_archive, pbi->cache_path->buffer, 0, 0, 0))
	{
		_r_show_errormessage (hwnd, NULL, zip_archive.m_last_error, mz_zip_get_error_string (zip_archive.m_last_error), ET_NONE);

		return FALSE;
	}

	total_files = mz_zip_reader_get_num_files (&zip_archive);

	// find root directory which contains main executable
	for (UINT i = 0; i < total_files; i++)
	{
		if (mz_zip_reader_is_file_a_directory (&zip_archive, i) || !mz_zip_reader_file_stat (&zip_archive, i, &file_stat))
			continue;

		if (file_stat.m_is_directory)
			continue;

		// count total size of unpacked files
		total_size += file_stat.m_uncomp_size;

		if (!root_dir_name)
		{
			_r_obj_initializebyteref (&path_sr, file_stat.m_filename);

			status = _r_str_multibyte2unicode (&path_sr, &path);

			if (!NT_SUCCESS (status))
				continue;

			_r_str_replacechar (&path->sr, OBJ_NAME_ALTPATH_SEPARATOR, OBJ_NAME_PATH_SEPARATOR);

			length = path->length - bin_name->length - separator_sr.length;

			if (_r_str_isendsswith (&path->sr, bin_name, TRUE) && path->buffer[length / sizeof (WCHAR)] == OBJ_NAME_PATH_SEPARATOR)
			{
				_r_obj_movereference ((PVOID_PTR)&root_dir_name, _r_obj_createstring_ex (path->buffer, path->length - bin_name->length));

				_r_str_trimstring (&root_dir_name->sr, &separator_sr, 0);
			}

			_r_obj_dereference (path);
		}
	}

	for (UINT i = 0; i < total_files; i++)
	{
		if (!mz_zip_reader_file_stat (&zip_archive, i, &file_stat))
			continue;

		_r_obj_initializebyteref (&path_sr, file_stat.m_filename);

		status = _r_str_multibyte2unicode (&path_sr, &path);

		if (!NT_SUCCESS (status))
			continue;

		_r_str_replacechar (&path->sr, OBJ_NAME_ALTPATH_SEPARATOR, OBJ_NAME_PATH_SEPARATOR);

		_r_str_trimstring (&path->sr, &separator_sr, 0);

		// skip non-root dirs
		if (!_r_obj_isstringempty (root_dir_name) && (path->length <= root_dir_name->length || !_r_str_isstartswith (&path->sr, &root_dir_name->sr, TRUE)))
			continue;

		if (root_dir_name)
			_r_str_skiplength (&path->sr, root_dir_name->length + separator_sr.length);

		dest_path = _r_obj_concatstringrefs (
			3,
			&pbi->binary_dir->sr,
			&separator_sr,
			&path->sr
		);

		_app_setstatus (hwnd, pbi->htaskbar, _r_locale_getstring (IDS_STATUS_INSTALL), total_read, total_size);

		if (mz_zip_reader_is_file_a_directory (&zip_archive, i))
		{
			_r_fs_createdirectory (&dest_path->sr);
		}
		else
		{
			sub_dir = _r_path_getbasedirectory (&dest_path->sr);

			if (sub_dir)
			{
				if (!_r_fs_exists (&sub_dir->sr))
					_r_fs_createdirectory (&sub_dir->sr);

				_r_obj_dereference (sub_dir);
			}

			if (mz_zip_reader_extract_to_file (&zip_archive, i, dest_path->buffer, MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY))
				total_read += file_stat.m_uncomp_size;
		}

		_r_obj_dereference (dest_path);

		if (!is_success)
			is_success = TRUE;
	}

	if (root_dir_name)
		_r_obj_dereference (root_dir_name);

	mz_zip_reader_end (&zip_archive);

	return is_success;
}

BOOLEAN _app_installupdate (
	_In_ HWND hwnd,
	_In_ PBROWSER_INFORMATION pbi,
	_Out_ PBOOLEAN is_error_ptr
)
{
	R_STRINGREF bin_name;
	PR_STRING buffer1;
	PR_STRING buffer2;
	NTSTATUS status;

	_r_queuedlock_acquireshared (&lock_download);

	status = _r_fs_deletedirectory (&pbi->binary_dir->sr, TRUE);

	if (!NT_SUCCESS (status) && status != STATUS_OBJECT_NAME_NOT_FOUND)
		_r_log (LOG_LEVEL_ERROR, NULL, L"_r_fs_deletedirectory", status, pbi->binary_dir->buffer);

	_r_path_getpathinfo (&pbi->binary_path->sr, NULL, &bin_name);

	_r_sys_setthreadexecutionstate (ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);

	if (!_r_fs_exists (&pbi->binary_dir->sr))
		_r_fs_createdirectory (&pbi->binary_dir->sr);

	if (_app_unpack_zip (hwnd, pbi, &bin_name))
	{
		status = SZ_OK;
	}
	else
	{
		status = _app_unpack_7zip (hwnd, pbi, &bin_name);
	}

	// get new version
	if (status == SZ_OK)
		_r_obj_movereference ((PVOID_PTR)&pbi->current_version, _r_res_queryversionstring (pbi->binary_path->buffer));

	// remove cache file when zip cannot be opened
	_r_fs_deletefile (&pbi->cache_path->sr, NULL);

	if (_r_fs_exists (&pbi->chrome_plus_dir->sr))
	{
		buffer1 = _r_format_string (L"%s\\version.dll", pbi->chrome_plus_dir->buffer);
		buffer2 = _r_format_string (L"%s\\version.dll", pbi->binary_dir->buffer);

		if (_r_fs_exists (&buffer1->sr))
			_r_fs_copyfile (&buffer1->sr, &buffer2->sr, FALSE);

		_r_obj_movereference ((PVOID_PTR)&buffer1, _r_format_string (L"%s\\chrome++.ini", pbi->chrome_plus_dir->buffer));
		_r_obj_movereference ((PVOID_PTR)&buffer2, _r_format_string (L"%s\\chrome++.ini", pbi->binary_dir->buffer));

		if (_r_fs_exists (&buffer1->sr))
			_r_fs_copyfile (&buffer1->sr, &buffer2->sr, FALSE);

		_r_obj_dereference (buffer1);
		_r_obj_dereference (buffer2);
	}

	*is_error_ptr = (status != SZ_OK);

	_r_queuedlock_releaseshared (&lock_download);

	_r_sys_setthreadexecutionstate (ES_CONTINUOUS);

	_app_setstatus (hwnd, pbi->htaskbar, NULL, 0, 0);

	return (status == SZ_OK) ? TRUE : FALSE;
}

static VOID _app_set_lastcheck (
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

static BOOLEAN _app_is_instance_configured (
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

static VOID _app_clear_browser_info_references (
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

static VOID _app_update_secondary_instance (
	_In_ HWND hwnd,
	_Inout_ PBROWSER_INFORMATION pbi
)
{
	BOOLEAN is_haveerror = FALSE;
	BOOLEAN is_exists;
	BOOLEAN is_updaterequired;

	if (!pbi || !pbi->binary_path)
		return;

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
		return;

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

static VOID _app_thread_taskupdate_all (
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
