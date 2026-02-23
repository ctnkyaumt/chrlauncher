// chrlauncher
// Copyright (c) 2015-2025 Henry++

#include "routine.h"

#include "main.h"
#include "rapp.h"

#include <tlhelp32.h>
#include <taskschd.h>
#include <oleauto.h>

static const WCHAR TASKUPDATE_TASK_NAME[] = L"chrlauncher\\AutoUpdate";

typedef struct _CLOSE_BROWSER_CONTEXT
{
	PBROWSER_INFORMATION pbi;
	ULONG count;
} CLOSE_BROWSER_CONTEXT, *PCLOSE_BROWSER_CONTEXT;

BOOL CALLBACK close_browser_window_callback (
	_In_ HWND hwnd,
	_In_ LPARAM lparam
)
{
	PCLOSE_BROWSER_CONTEXT ctx;
	PR_STRING process_path;
	HANDLE hprocess;
	ULONG pid;
	NTSTATUS status;

	ctx = (PCLOSE_BROWSER_CONTEXT)lparam;

	GetWindowThreadProcessId (hwnd, &pid);

	if (HandleToULong (NtCurrentProcessId ()) == pid)
		return TRUE;

	status = _r_sys_openprocess ((HANDLE)(ULONG_PTR)pid, PROCESS_QUERY_LIMITED_INFORMATION, &hprocess);

	if (!NT_SUCCESS (status))
		return TRUE;

	status = _r_sys_queryprocessstring (hprocess, ProcessImageFileNameWin32, &process_path);

	if (NT_SUCCESS (status))
	{
		if (_r_str_isequal (&ctx->pbi->binary_path->sr, &process_path->sr, TRUE))
		{
			PostMessageW (hwnd, WM_CLOSE, 0, 0);
			ctx->count += 1;
		}

		_r_obj_dereference (process_path);
	}

	NtClose (hprocess);

	return TRUE;
}

static BOOLEAN _app_taskupdate_terminateprocesses (
	_In_ PBROWSER_INFORMATION pbi
)
{
	HANDLE hsnapshot;
	PROCESSENTRY32W pe32 = {0};
	PR_STRING process_path;
	HANDLE hprocess;
	ULONG terminate_count = 0;
	NTSTATUS status;

	hsnapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);

	if (hsnapshot == INVALID_HANDLE_VALUE)
		return FALSE;

	pe32.dwSize = sizeof (pe32);

	if (Process32FirstW (hsnapshot, &pe32))
	{
		do
		{
			if ((HANDLE)(ULONG_PTR)pe32.th32ProcessID == NtCurrentProcessId ())
				continue;

			status = _r_sys_openprocess ((HANDLE)(ULONG_PTR)pe32.th32ProcessID, PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, &hprocess);

			if (!NT_SUCCESS (status))
				continue;

			status = _r_sys_queryprocessstring (hprocess, ProcessImageFileNameWin32, &process_path);

			if (NT_SUCCESS (status))
			{
				if (_r_str_isequal (&pbi->binary_path->sr, &process_path->sr, TRUE))
				{
					if (TerminateProcess (hprocess, 0))
						terminate_count += 1;
				}

				_r_obj_dereference (process_path);
			}

			NtClose (hprocess);
		}
		while (Process32NextW (hsnapshot, &pe32));
	}

	CloseHandle (hsnapshot);

	return (terminate_count != 0);
}

VOID _app_taskupdate_closebrowser (
	_In_ PBROWSER_INFORMATION pbi,
	_Out_ PBOOLEAN was_running_ptr
)
{
	CLOSE_BROWSER_CONTEXT close_ctx = {0};
	BOOLEAN was_running = FALSE;

	close_ctx.pbi = pbi;

	EnumWindows (&close_browser_window_callback, (LPARAM)&close_ctx);

	if (close_ctx.count != 0 || _r_fs_isfileused (&pbi->binary_path->sr))
	{
		_r_tray_popup (pbi->hwnd, &GUID_TrayIcon, NIIF_INFO, _r_app_getname (), _r_locale_getstring (IDS_STATUS_RESTARTNOTICE));

		was_running = TRUE;

		for (INT i = 0; i < 40; i++)
		{
			if (!_r_fs_isfileused (&pbi->binary_path->sr))
				break;

			Sleep (500);
		}

		if (_r_fs_isfileused (&pbi->binary_path->sr))
		{
			_app_taskupdate_terminateprocesses (pbi);

			for (INT i = 0; i < 40; i++)
			{
				if (!_r_fs_isfileused (&pbi->binary_path->sr))
					break;

				Sleep (500);
			}
		}
	}

	*was_running_ptr = was_running;
}

static BOOLEAN _app_runprocess_wait (
	_In_ LPCWSTR cmdline,
	_Out_opt_ PULONG exit_code_ptr
)
{
	STARTUPINFOW si = {0};
	PROCESS_INFORMATION pi = {0};
	LPWSTR mutable_cmdline;
	SIZE_T length;
	ULONG exit_code = ERROR_GEN_FAILURE;
	BOOLEAN is_success = FALSE;

	si.cb = sizeof (si);

	length = _r_str_getlength (cmdline) + 1;
	mutable_cmdline = _r_mem_allocate (length * sizeof (WCHAR));

	if (!mutable_cmdline)
		return FALSE;

	_r_str_copy (mutable_cmdline, length, cmdline);

	if (CreateProcessW (
		NULL,
		mutable_cmdline,
		NULL,
		NULL,
		FALSE,
		CREATE_NO_WINDOW,
		NULL,
		NULL,
		&si,
		&pi
	))
	{
		WaitForSingleObject (pi.hProcess, INFINITE);

		if (GetExitCodeProcess (pi.hProcess, &exit_code))
			is_success = TRUE;

		CloseHandle (pi.hThread);
		CloseHandle (pi.hProcess);
	}

	_r_mem_free (mutable_cmdline);

	if (exit_code_ptr)
		*exit_code_ptr = exit_code;

	return is_success;
}

BOOLEAN _app_taskupdate_istaskpresent ()
{
	PR_STRING cmdline;
	ULONG exit_code = ERROR_GEN_FAILURE;
	BOOLEAN is_exists = FALSE;

	cmdline = _r_format_string (L"schtasks.exe /Query /TN \"%s\"", TASKUPDATE_TASK_NAME);

	if (cmdline)
	{
		if (_app_runprocess_wait (cmdline->buffer, &exit_code))
			is_exists = (exit_code == ERROR_SUCCESS);

		_r_obj_dereference (cmdline);
	}

	return is_exists;
}

BOOLEAN _app_taskupdate_setstartwhenavailable ()
{
	ITaskService *task_service = NULL;
	ITaskFolder *task_folder = NULL;
	IRegisteredTask *registered_task = NULL;
	ITaskDefinition *task_definition = NULL;
	ITaskSettings *task_settings = NULL;
	IPrincipal *task_principal = NULL;
	IRegisteredTask *registered_task_new = NULL;

	BSTR folder_path = NULL;
	BSTR task_name = NULL;
	BSTR user_id = NULL;

	VARIANT var_empty = {0};
	VARIANT var_user = {0};
	VARIANT var_sddl = {0};

	CLSID task_service_clsid = {0};
	IID task_service_iid = {0};

	HRESULT status;
	TASK_LOGON_TYPE logon_type = TASK_LOGON_INTERACTIVE_TOKEN;
	BOOLEAN is_success = FALSE;

	VariantInit (&var_empty);
	VariantInit (&var_user);
	VariantInit (&var_sddl);

	var_empty.vt = VT_EMPTY;
	var_sddl.vt = VT_EMPTY;

	status = CLSIDFromString (L"{0F87369F-A4E5-4CFC-BD3E-73E6154572DD}", &task_service_clsid);

	if (FAILED (status))
		goto CleanupExit;

	status = IIDFromString (L"{2FABA4C7-4DA9-4013-9697-20CC3FD40F85}", &task_service_iid);

	if (FAILED (status))
		goto CleanupExit;

	status = CoCreateInstance (&task_service_clsid, NULL, CLSCTX_INPROC_SERVER, &task_service_iid, (PVOID_PTR)&task_service);

	if (FAILED (status) || !task_service)
		goto CleanupExit;

	status = task_service->lpVtbl->Connect (task_service, var_empty, var_empty, var_empty, var_empty);

	if (FAILED (status))
		goto CleanupExit;

	folder_path = SysAllocString (L"\\chrlauncher");
	task_name = SysAllocString (L"AutoUpdate");

	if (!folder_path || !task_name)
		goto CleanupExit;

	status = task_service->lpVtbl->GetFolder (task_service, folder_path, &task_folder);

	if (FAILED (status) || !task_folder)
		goto CleanupExit;

	status = task_folder->lpVtbl->GetTask (task_folder, task_name, &registered_task);

	if (FAILED (status) || !registered_task)
		goto CleanupExit;

	status = registered_task->lpVtbl->get_Definition (registered_task, &task_definition);

	if (FAILED (status) || !task_definition)
		goto CleanupExit;

	status = task_definition->lpVtbl->get_Settings (task_definition, &task_settings);

	if (FAILED (status) || !task_settings)
		goto CleanupExit;

	status = task_settings->lpVtbl->put_StartWhenAvailable (task_settings, VARIANT_TRUE);

	if (FAILED (status))
		goto CleanupExit;

	status = task_definition->lpVtbl->get_Principal (task_definition, &task_principal);

	if (SUCCEEDED (status) && task_principal)
	{
		status = task_principal->lpVtbl->get_LogonType (task_principal, &logon_type);

		if (FAILED (status))
			logon_type = TASK_LOGON_INTERACTIVE_TOKEN;

		status = task_principal->lpVtbl->get_UserId (task_principal, &user_id);

		if (SUCCEEDED (status) && user_id)
		{
			var_user.vt = VT_BSTR;
			var_user.bstrVal = user_id;
			user_id = NULL;
		}
	}

	status = task_folder->lpVtbl->RegisterTaskDefinition (
		task_folder,
		task_name,
		task_definition,
		TASK_CREATE_OR_UPDATE,
		var_user,
		var_empty,
		logon_type,
		var_sddl,
		&registered_task_new
	);

	if (FAILED (status) || !registered_task_new)
		goto CleanupExit;

	is_success = TRUE;

CleanupExit:
	if (registered_task_new)
		registered_task_new->lpVtbl->Release (registered_task_new);

	if (task_principal)
		task_principal->lpVtbl->Release (task_principal);

	if (task_settings)
		task_settings->lpVtbl->Release (task_settings);

	if (task_definition)
		task_definition->lpVtbl->Release (task_definition);

	if (registered_task)
		registered_task->lpVtbl->Release (registered_task);

	if (task_folder)
		task_folder->lpVtbl->Release (task_folder);

	if (task_service)
		task_service->lpVtbl->Release (task_service);

	VariantClear (&var_user);
	VariantClear (&var_empty);
	VariantClear (&var_sddl);

	if (folder_path)
		SysFreeString (folder_path);

	if (task_name)
		SysFreeString (task_name);

	if (user_id)
		SysFreeString (user_id);

	return is_success;
}

BOOLEAN _app_taskupdate_createtask ()
{
	WCHAR exe_path[4096] = {0};
	PR_STRING cmdline;
	ULONG exit_code = ERROR_GEN_FAILURE;
	BOOLEAN is_success = FALSE;

	if (!GetModuleFileNameW (NULL, exe_path, RTL_NUMBER_OF (exe_path)))
		return FALSE;

	cmdline = _r_format_string (
		L"schtasks.exe /Create /TN \"%s\" /SC DAILY /MO 14 /ST 00:00 /F /RL LIMITED /TR \"\\\"%s\\\" /taskupdate\"",
		TASKUPDATE_TASK_NAME,
		exe_path
	);

	if (cmdline)
	{
		if (_app_runprocess_wait (cmdline->buffer, &exit_code))
			is_success = (exit_code == ERROR_SUCCESS);

		_r_obj_dereference (cmdline);
	}

	if (is_success)
		is_success = _app_taskupdate_setstartwhenavailable ();

	return is_success;
}

BOOLEAN _app_taskupdate_deletetask ()
{
	PR_STRING cmdline;
	ULONG exit_code = ERROR_GEN_FAILURE;
	BOOLEAN is_success = FALSE;

	cmdline = _r_format_string (L"schtasks.exe /Delete /TN \"%s\" /F", TASKUPDATE_TASK_NAME);

	if (cmdline)
	{
		if (_app_runprocess_wait (cmdline->buffer, &exit_code))
			is_success = (exit_code == ERROR_SUCCESS);

		_r_obj_dereference (cmdline);
	}

	return is_success;
}
