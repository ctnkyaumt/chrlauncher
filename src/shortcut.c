// chrlauncher
// Copyright (c) 2015-2025 Henry++

#include "routine.h"

#include "main.h"
#include "rapp.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <propsys.h>
#include <propkey.h>

VOID _app_create_profileshortcut (
	_In_ PBROWSER_INFORMATION pbi
)
{
	PWSTR desktop_path = NULL;
	PR_STRING link_title = NULL;
	PR_STRING link_path = NULL;
	PR_STRING app_id = NULL;
	HRESULT hr_init;
	HRESULT hr;
	IShellLinkW *psl = NULL;
	IPersistFile *ppf = NULL;
	IPropertyStore *pps = NULL;
	PROPVARIANT pv = {0};

	if (!pbi || _r_obj_isstringempty (pbi->binary_path) || _r_obj_isstringempty (pbi->profile_dir))
		return;

	if (!_r_fs_exists (&pbi->binary_path->sr))
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

	hr = SHGetKnownFolderPath (&FOLDERID_Desktop, KF_FLAG_DEFAULT, NULL, &desktop_path);

	if (FAILED (hr) || !desktop_path)
		return;

	if (pbi->instance_id <= 1)
		link_title = _r_format_string (L"%s (%" TEXT (PR_LONG) L"-bit)", _r_obj_getstring (pbi->browser_type), pbi->architecture);
	else
		link_title = _r_format_string (L"%s (%" TEXT (PR_LONG) L"-bit) #%" TEXT (PR_LONG), _r_obj_getstring (pbi->browser_type), pbi->architecture, pbi->instance_id);

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

	hr_init = CoInitializeEx (NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

	hr = CoCreateInstance (&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (PVOID_PTR)&psl);

	if (SUCCEEDED (hr) && psl)
	{
		psl->lpVtbl->SetPath (psl, pbi->binary_path->buffer);

		if (!_r_obj_isstringempty (pbi->binary_dir))
			psl->lpVtbl->SetWorkingDirectory (psl, pbi->binary_dir->buffer);

		if (!_r_obj_isstringempty (pbi->args_str))
			psl->lpVtbl->SetArguments (psl, pbi->args_str->buffer);

		psl->lpVtbl->SetIconLocation (psl, pbi->binary_path->buffer, 0);
		psl->lpVtbl->SetDescription (psl, link_title->buffer);

		app_id = _r_format_string (
			L"%s.%s.%" TEXT (PR_LONG) L".%" TEXT (PR_LONG),
			_r_app_getnameshort (),
			_r_obj_getstring (pbi->browser_type),
			pbi->architecture,
			(pbi->instance_id >= 1) ? pbi->instance_id : 1
		);

		if (app_id)
		{
			hr = psl->lpVtbl->QueryInterface (psl, &IID_IPropertyStore, (PVOID_PTR)&pps);

			if (SUCCEEDED (hr) && pps)
			{
				pv.vt = VT_LPWSTR;
				pv.pwszVal = CoTaskMemAlloc (app_id->length + sizeof (WCHAR));

				if (pv.pwszVal)
				{
					RtlCopyMemory (pv.pwszVal, app_id->buffer, app_id->length + sizeof (WCHAR));
					pps->lpVtbl->SetValue (pps, &PKEY_AppUserModel_ID, &pv);
					pps->lpVtbl->Commit (pps);
					PropVariantClear (&pv);
				}

				pps->lpVtbl->Release (pps);
			}

			_r_obj_dereference (app_id);
		}

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
