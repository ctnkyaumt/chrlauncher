// chrlauncher
// Copyright (c) 2015-2025 Henry++

#include "routine.h"

#include "main.h"
#include "rapp.h"

static BOOL CALLBACK _app_activate_browser_window_callback (
	_In_ HWND hwnd,
	_In_ LPARAM lparam
)
{
	PBROWSER_INFORMATION pbi;
	PR_STRING process_path;
	HANDLE hprocess;
	ULONG pid;
	BOOL is_success = TRUE;
	NTSTATUS status;

	GetWindowThreadProcessId (hwnd, &pid);

	if (HandleToULong (NtCurrentProcessId ()) == pid)
		return TRUE;

	if (!_r_wnd_isvisible (hwnd, FALSE))
		return TRUE;

	status = _r_sys_openprocess ((HANDLE)(ULONG_PTR)pid, PROCESS_QUERY_LIMITED_INFORMATION, &hprocess);

	if (!NT_SUCCESS (status))
		return TRUE;

	status = _r_sys_queryprocessstring (hprocess, ProcessImageFileNameWin32, &process_path);

	if (NT_SUCCESS (status))
	{
		pbi = (PBROWSER_INFORMATION)lparam;

		if (_r_str_isequal (&pbi->binary_path->sr, &process_path->sr, TRUE))
		{
			_r_wnd_toggle (hwnd, TRUE);

			is_success = FALSE;
		}

		_r_obj_dereference (process_path);
	}

	NtClose (hprocess);

	return is_success;
}

VOID _app_openbrowser (
	_In_ PBROWSER_INFORMATION pbi
)
{
	PR_STRING args_string;
	PR_STRING cmdline;
	LPWSTR ptr;
	ULONG_PTR args_length = 0;
	NTSTATUS status;

	if (_r_obj_isstringempty (pbi->binary_path) || !_r_fs_exists (&pbi->binary_path->sr))
	{
		_r_show_errormessage (_r_app_gethwnd (), NULL, STATUS_OBJECT_PATH_NOT_FOUND, _r_obj_getstring (pbi->binary_path), ET_NATIVE);

		return;
	}

	if (_r_fs_isfileused (&pbi->binary_path->sr) && !pbi->is_hasurls && !pbi->is_opennewwindow)
	{
		EnumWindows (&_app_activate_browser_window_callback, (LPARAM)pbi);

		return;
	}

	if (pbi->args_str)
		args_length += pbi->args_str->length;

	if (pbi->is_hasurls && pbi->urls_str)
		args_length += pbi->urls_str->length;

	args_length += sizeof (WCHAR); // for space

	args_string = _r_obj_createstring_ex (NULL, args_length);

	if (pbi->args_str)
		RtlCopyMemory (args_string->buffer, pbi->args_str->buffer, pbi->args_str->length);

	if (pbi->is_hasurls)
	{
		if (pbi->urls_str)
		{
			if (pbi->args_str)
			{
				ptr = PTR_ADD_OFFSET (args_string->buffer, pbi->args_str->length);
				RtlCopyMemory (ptr, L" ", sizeof (WCHAR)); // insert space

				ptr = PTR_ADD_OFFSET (args_string->buffer, pbi->args_str->length + sizeof (WCHAR));
				RtlCopyMemory (ptr, pbi->urls_str->buffer, pbi->urls_str->length);
			}
			else
			{
				RtlCopyMemory (args_string->buffer, pbi->urls_str->buffer, pbi->urls_str->length);
			}

			_r_obj_clearreference ((PVOID_PTR)&pbi->urls_str);
		}

		pbi->is_hasurls = FALSE; // reset
	}

	_r_str_trimtonullterminator (&args_string->sr);

	pbi->is_opennewwindow = FALSE;

	cmdline = _r_format_string (L"\"%s\" -url %s", pbi->binary_path->buffer, args_string->buffer);

	status = _r_sys_createprocess (pbi->binary_path->buffer, cmdline->buffer, pbi->binary_dir->buffer, FALSE);

	if (!NT_SUCCESS (status))
		_r_show_errormessage (_r_app_gethwnd (), NULL, status, pbi->binary_path->buffer, ET_NATIVE);

	_r_obj_dereference (args_string);
	_r_obj_dereference (cmdline);
}
