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

extern R_QUEUED_LOCK lock_download;

VOID _app_setstatus (
	_In_ HWND hwnd,
	_In_opt_ HWND htaskbar,
	_In_opt_ LPCWSTR string,
	_In_opt_ ULONG64 total_read,
	_In_opt_ ULONG64 total_length
);

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
