// launchbro
// 2026 ctnkyaumt

#include "routine.h"

#include "main.h"
#include "rapp.h"

#include "resource.h"
#include <shlobj.h>
#include <shellapi.h>

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

	slash_ptr = wcsrchr (dir_buf, L'\\');
	if (slash_ptr)
	{
		LPCWSTR leaf = slash_ptr + 1;
		if (_r_str_compare (leaf, L"32", 2) == 0 || _r_str_compare (leaf, L"64", 2) == 0)
			*slash_ptr = 0;
	}

	return _r_obj_createstring (dir_buf);
}

static PR_STRING _app_browse_for_folder (
	_In_ HWND hwnd,
	_In_ LPCWSTR title
)
{
	PR_STRING result = NULL;
	BROWSEINFOW bi = {0};
	LPITEMIDLIST pidl;
	WCHAR buffer[MAX_PATH] = {0};

	bi.hwndOwner = hwnd;
	bi.lpszTitle = title;
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

	pidl = SHBrowseForFolderW (&bi);

	if (pidl)
	{
		if (SHGetPathFromIDListW (pidl, buffer))
		{
			result = _r_obj_createstring (buffer);
		}

		CoTaskMemFree (pidl);
	}

	return result;
}

static PR_STRING _app_get_profile_label (
	_In_ LONG instance_id,
	_In_ PR_STRING profile_dir
)
{
	PR_STRING browser_type = NULL;
	PR_STRING label;

	if (instance_id >= 2 && instance_id <= 4)
	{
		PR_STRING type_key = _r_format_string (L"ChromiumType%" TEXT (PR_LONG), instance_id);
		if (type_key)
		{
			browser_type = _r_config_getstring (type_key->buffer, NULL);
			_r_obj_dereference (type_key);
		}
	}

	if (!browser_type)
		browser_type = _r_config_getstring (L"ChromiumType", L"chromium");

	label = _r_format_string (L"launchbro_profile%" TEXT (PR_LONG) L"_%s", instance_id, browser_type ? browser_type->buffer : L"chromium");

	if (browser_type)
		_r_obj_dereference (browser_type);

	return label;
}

static BOOLEAN _app_copy_directory (
	_In_ PR_STRING src,
	_In_ PR_STRING dst
)
{
	PR_STRING search_path;
	PR_STRING src_file;
	PR_STRING dst_file;
	HANDLE hfind;
	WIN32_FIND_DATAW find_data;
	BOOLEAN is_success = TRUE;

	_r_fs_createdirectory (&dst->sr);

	search_path = _r_format_string (L"%s\\*", src->buffer);

	if (!search_path)
		return FALSE;

	hfind = FindFirstFileW (search_path->buffer, &find_data);

	if (hfind != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (_r_str_compare (find_data.cFileName, L".", 1) == 0 ||
				_r_str_compare (find_data.cFileName, L"..", 2) == 0)
			{
				continue;
			}

			src_file = _r_format_string (L"%s\\%s", src->buffer, find_data.cFileName);
			dst_file = _r_format_string (L"%s\\%s", dst->buffer, find_data.cFileName);

			if (!src_file || !dst_file)
			{
				is_success = FALSE;
				break;
			}

			if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				if (!_app_copy_directory (src_file, dst_file))
					is_success = FALSE;
			}
			else
			{
				_r_fs_copyfile (&src_file->sr, &dst_file->sr, FALSE);
			}

			_r_obj_dereference (src_file);
			_r_obj_dereference (dst_file);
		}
		while (FindNextFileW (hfind, &find_data));

		FindClose (hfind);
	}

	_r_obj_dereference (search_path);

	return is_success;
}

VOID _app_export_profile (
	_In_ HWND hwnd
)
{
	PR_STRING app_root;
	PR_STRING export_dir;
	PR_STRING profile_dir;
	PR_STRING export_subdir;
	PR_STRING label;
	LONG instance_id;
	BOOLEAN any_exported = FALSE;

	app_root = _app_get_app_root_dir ();

	if (!app_root)
		return;

	export_dir = _app_browse_for_folder (hwnd, _r_locale_getstring (IDS_EXPORTPROFILE_TITLE));

	if (!export_dir)
	{
		_r_obj_dereference (app_root);
		return;
	}

	for (instance_id = 1; instance_id <= 4; instance_id++)
	{
		if (instance_id == 1)
			profile_dir = _r_format_string (L"%s\\profile", app_root->buffer);
		else
			profile_dir = _r_format_string (L"%s\\profile%" TEXT (PR_LONG), app_root->buffer, instance_id);

		if (!profile_dir)
			continue;

		if (!_r_fs_exists (&profile_dir->sr))
		{
			_r_obj_dereference (profile_dir);
			continue;
		}

		label = _app_get_profile_label (instance_id, profile_dir);

		if (!label)
		{
			_r_obj_dereference (profile_dir);
			continue;
		}

		export_subdir = _r_format_string (L"%s\\%s", export_dir->buffer, label->buffer);

		if (export_subdir)
		{
			_app_copy_directory (profile_dir, export_subdir);
			any_exported = TRUE;
			_r_obj_dereference (export_subdir);
		}

		_r_obj_dereference (label);
		_r_obj_dereference (profile_dir);
	}

	if (any_exported)
		_r_show_message (hwnd, MB_OK | MB_ICONINFORMATION, NULL, _r_locale_getstring (IDS_PROFILE_EXPORTED));

	_r_obj_dereference (export_dir);
	_r_obj_dereference (app_root);
}

VOID _app_import_profile (
	_In_ HWND hwnd
)
{
	PR_STRING app_root;
	PR_STRING import_dir;
	PR_STRING profile_dir;
	PR_STRING import_subdir;
	LONG instance_id;
	INT selected_instance;
	BOOLEAN any_imported = FALSE;

	app_root = _app_get_app_root_dir ();

	if (!app_root)
		return;

	import_dir = _app_browse_for_folder (hwnd, _r_locale_getstring (IDS_IMPORTPROFILE_TITLE));

	if (!import_dir)
	{
		_r_obj_dereference (app_root);
		return;
	}

	// Ask user which instance to import into (1-4)
	selected_instance = _r_show_message (hwnd, MB_YESNOCANCEL | MB_ICONQUESTION, NULL,
		L"Import into instance 1?\n\n"
		L"Yes = Instance 1\n"
		L"No = Other instances (will prompt)\n"
		L"Cancel = Abort");

	if (selected_instance == IDCANCEL)
	{
		_r_obj_dereference (import_dir);
		_r_obj_dereference (app_root);
		return;
	}

	if (selected_instance == IDYES)
	{
		instance_id = 1;
	}
	else
	{
		// Simple dialog for other instances
		PR_STRING input = NULL;
		// Since we don't have a custom input dialog easily, use a simple message-based approach
		// Ask for instance 2, 3, or 4 using multiple message boxes
		if (_r_show_message (hwnd, MB_YESNOCANCEL | MB_ICONQUESTION, NULL, L"Import into instance 2?\n\nYes = Instance 2, No = Instance 3, Cancel = Instance 4") == IDYES)
		{
			instance_id = 2;
		}
		else if (_r_show_message (hwnd, MB_YESNOCANCEL | MB_ICONQUESTION, NULL, L"Import into instance 3?\n\nYes = Instance 3, No = Instance 4, Cancel = Abort") == IDYES)
		{
			instance_id = 3;
		}
		else if (_r_show_message (hwnd, MB_YESNOCANCEL | MB_ICONQUESTION, NULL, L"Import into instance 4?\n\nYes = Instance 4, No = Abort, Cancel = Abort") == IDYES)
		{
			instance_id = 4;
		}
		else
		{
			_r_obj_dereference (import_dir);
			_r_obj_dereference (app_root);
			return;
		}
	}

	if (instance_id == 1)
		profile_dir = _r_format_string (L"%s\\profile", app_root->buffer);
	else
		profile_dir = _r_format_string (L"%s\\profile%" TEXT (PR_LONG), app_root->buffer, instance_id);

	if (profile_dir)
	{
		// Check if target exists and confirm overwrite
		if (_r_fs_exists (&profile_dir->sr))
		{
			if (_r_show_message (hwnd, MB_YESNO | MB_ICONWARNING, NULL,
				L"Target profile already exists. Overwrite?") != IDYES)
			{
				_r_obj_dereference (profile_dir);
				_r_obj_dereference (import_dir);
				_r_obj_dereference (app_root);
				return;
			}
		}

		_r_fs_createdirectory (&profile_dir->sr);

		import_subdir = _r_format_string (L"%s\\launchbro_profile%" TEXT (PR_LONG), import_dir->buffer, instance_id);

		if (!import_subdir || !_r_fs_exists (&import_subdir->sr))
		{
			// Try generic profile folder name
			if (import_subdir)
				_r_obj_dereference (import_subdir);

			import_subdir = _r_format_string (L"%s\\profile%" TEXT (PR_LONG), import_dir->buffer, instance_id);
		}

		if (!import_subdir || !_r_fs_exists (&import_subdir->sr))
		{
			if (import_subdir)
				_r_obj_dereference (import_subdir);

			import_subdir = _r_format_string (L"%s\\profile", import_dir->buffer);
		}

		if (import_subdir && _r_fs_exists (&import_subdir->sr))
		{
			_app_copy_directory (import_subdir, profile_dir);
			any_imported = TRUE;
			_r_obj_dereference (import_subdir);
		}

		_r_obj_dereference (profile_dir);
	}

	if (any_imported)
		_r_show_message (hwnd, MB_OK | MB_ICONINFORMATION, NULL, _r_locale_getstring (IDS_PROFILE_IMPORTED));

	_r_obj_dereference (import_dir);
	_r_obj_dereference (app_root);
}

static BOOLEAN _app_delete_directory_contents (
	_In_ PR_STRING dir
)
{
	PR_STRING search_path;
	PR_STRING file_path;
	HANDLE hfind;
	WIN32_FIND_DATAW find_data;
	BOOLEAN is_success = TRUE;

	search_path = _r_format_string (L"%s\\*", dir->buffer);

	if (!search_path)
		return FALSE;

	hfind = FindFirstFileW (search_path->buffer, &find_data);

	if (hfind != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (_r_str_compare (find_data.cFileName, L".", 1) == 0 ||
				_r_str_compare (find_data.cFileName, L"..", 2) == 0)
			{
				continue;
			}

			file_path = _r_format_string (L"%s\\%s", dir->buffer, find_data.cFileName);

			if (!file_path)
			{
				is_success = FALSE;
				continue;
			}

			if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				if (!_app_delete_directory_contents (file_path))
					is_success = FALSE;

				RemoveDirectoryW (file_path->buffer);
			}
			else
			{
				_r_fs_deletefile (&file_path->sr, NULL);
			}

			_r_obj_dereference (file_path);
		}
		while (FindNextFileW (hfind, &find_data));

		FindClose (hfind);
	}

	_r_obj_dereference (search_path);

	return is_success;
}

VOID _app_uninstall_app (
	_In_ HWND hwnd
)
{
	PR_STRING app_root;
	PR_STRING bat_path = NULL;
	PR_STRING bat_content = NULL;
	HANDLE hfile;
	ULONG written;
	NTSTATUS status;
	BOOLEAN keep_profiles = FALSE;
	SHELLEXECUTEINFOW sei = {0};
	INT msg_result;
	WCHAR exe_path[4096] = {0};

	msg_result = _r_show_message (hwnd, MB_YESNO | MB_ICONWARNING, NULL, _r_locale_getstring (IDS_UNINSTALL_CONFIRM));

	if (msg_result != IDYES)
		return;

	msg_result = _r_show_message (hwnd, MB_YESNO | MB_ICONQUESTION, NULL, _r_locale_getstring (IDS_UNINSTALL_KEEPPROFILES));

	if (msg_result == IDYES)
		keep_profiles = TRUE;

	app_root = _app_get_app_root_dir ();

	if (!app_root)
		return;

	bat_path = _r_format_string (L"%s\\_launchbro_uninstall.bat", app_root->buffer);

	if (!bat_path)
		goto CleanupExit;

	if (keep_profiles)
	{
		bat_content = _r_format_string (
			L"@echo off\r\n"
			L"timeout /t 2 /nobreak >nul\r\n"
			L"set \"ROOT=%s\"\r\n"
			L"powershell -NoProfile -ExecutionPolicy Bypass -Command \"$roots=@($env:ROOT);$legacy=Join-Path ([System.IO.Path]::GetDirectoryName($env:ROOT)) 'chrlauncher';if(Test-Path $legacy){$roots+=$legacy};Get-CimInstance Win32_Process | Where-Object { $path=$_.ExecutablePath; $path -and @($roots | Where-Object { $path.StartsWith($_, [System.StringComparison]::OrdinalIgnoreCase) }).Count -gt 0 } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }\" >nul 2>&1\r\n"
			L":wait_loop\r\n"
			L"tasklist | findstr /i \"launchbro.exe\" >nul\r\n"
			L"if %%errorlevel%% == 0 (\r\n"
			L"  timeout /t 1 /nobreak >nul\r\n"
			L"  goto wait_loop\r\n"
			L")\r\n"
			L"\r\n"
			L"for /d %%d in (\"%%ROOT%%\\32\",\"%%ROOT%%\\64\") do (\r\n"
			L"  if exist \"%%d\" (\r\n"
			L"    for %%f in (\"%%d\\*.exe\",\"%%d\\*.dll\") do del /f /q \"%%f\" >nul 2>&1\r\n"
			L"  )\r\n"
			L")\r\n"
			L"del /f /q \"%%ROOT%%\\*.exe\" >nul 2>&1\r\n"
			L"del /f /q \"%%ROOT%%\\*.dll\" >nul 2>&1\r\n"
			L"del /f /q \"%%ROOT%%\\*.bat\" >nul 2>&1\r\n"
			L"del /f /q \"%%ROOT%%\\*.ini\" >nul 2>&1\r\n"
			L"del /f /q \"%%ROOT%%\\*.lng\" >nul 2>&1\r\n"
			L"del /f /q \"%%ROOT%%\\*.txt\" >nul 2>&1\r\n"
			L"rmdir /s /q \"%%ROOT%%\\i18n\" >nul 2>&1\r\n"
			L"del /f /q \"%s\"\r\n",
			app_root->buffer,
			bat_path->buffer
		);
	}
	else
	{
		bat_content = _r_format_string (
			L"@echo off\r\n"
			L"timeout /t 2 /nobreak >nul\r\n"
			L"set \"ROOT=%s\"\r\n"
			L"powershell -NoProfile -ExecutionPolicy Bypass -Command \"$roots=@($env:ROOT);$legacy=Join-Path ([System.IO.Path]::GetDirectoryName($env:ROOT)) 'chrlauncher';if(Test-Path $legacy){$roots+=$legacy};Get-CimInstance Win32_Process | Where-Object { $path=$_.ExecutablePath; $path -and @($roots | Where-Object { $path.StartsWith($_, [System.StringComparison]::OrdinalIgnoreCase) }).Count -gt 0 } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }\" >nul 2>&1\r\n"
			L":wait_loop\r\n"
			L"tasklist | findstr /i \"launchbro.exe\" >nul\r\n"
			L"if %%errorlevel%% == 0 (\r\n"
			L"  timeout /t 1 /nobreak >nul\r\n"
			L"  goto wait_loop\r\n"
			L")\r\n"
			L"\r\n"
			L"rmdir /s /q \"%%ROOT%%\"\r\n"
			L"del /f /q \"%s\"\r\n",
			app_root->buffer,
			bat_path->buffer
		);
	}

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

	// Delete scheduled task if present
	_app_taskupdate_deletetask ();

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

