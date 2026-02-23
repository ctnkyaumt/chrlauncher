// chrlauncher
// Copyright (c) 2015-2025 Henry++

#include "routine.h"

#include "main.h"
#include "rapp.h"

#include <stdlib.h>

static BOOLEAN _app_path_is_url (
	_In_ LPCWSTR path
)
{
	LPCWSTR types[] = {
		L"application/pdf",
		L"image/svg+xml",
		L"image/webp",
		L"text/html",
	};

	if (PathMatchSpecW (path, L"*.ini"))
		return FALSE;

	if (PathIsURLW (path) || PathIsContentTypeW (path, SZ_CONTENTTYPE_HTMLW))
		return TRUE;

	for (ULONG_PTR i = 0; i < RTL_NUMBER_OF (types); i++)
	{
		if (PathIsContentTypeW (path, types[i]))
			return TRUE;
	}

	return FALSE;
}

VOID _app_parse_args (
	_Inout_ PBROWSER_INFORMATION pbi
)
{
	LPWSTR *arga;
	LPWSTR key;
	LPWSTR key2;
	LPWSTR value_ptr;
	ULONG_PTR first_arg_length = 0;
	INT numargs;

	arga = CommandLineToArgvW (_r_sys_getcommandline (), &numargs);

	if (!arga)
		return;

	first_arg_length = _r_str_getlength (arga[0]);

	if (numargs > 1)
	{
		for (INT i = 1; i < numargs; i++)
		{
			key = arga[i];

			if (*key == L'/' || *key == L'-')
			{
				key2 = PTR_ADD_OFFSET (key, sizeof (WCHAR));

				if (_r_str_compare (key2, L"autodownload", 12) == 0)
				{
					pbi->is_autodownload = TRUE;
				}
				else if (_r_str_compare (key2, L"arch", 4) == 0 || _r_str_compare (key2, L"architecture", 12) == 0)
				{
					value_ptr = NULL;

					if (key2[4] == L'=' || key2[4] == L':')
					{
						value_ptr = PTR_ADD_OFFSET (key2, (4 + 1) * sizeof (WCHAR));
					}
					else if (key2[4] == 0 && i + 1 < numargs)
					{
						value_ptr = arga[++i];
					}

					if (value_ptr)
					{
						LONG arch = (LONG)wcstol (value_ptr, NULL, 10);

						if ((pbi->architecture != 32 && pbi->architecture != 64) && (arch == 32 || arch == 64))
							pbi->architecture = arch;
					}
				}
				else if (_r_str_compare (key2, L"bringtofront", 12) == 0)
				{
					pbi->is_bringtofront = TRUE;
				}
				else if (_r_str_compare (key2, L"forcecheck", 10) == 0)
				{
					pbi->is_forcecheck = TRUE;
				}
				else if (_r_str_compare (key2, L"instance", 8) == 0)
				{
					value_ptr = NULL;

					if (key2[8] == L'=' || key2[8] == L':')
					{
						value_ptr = PTR_ADD_OFFSET (key2, (8 + 1) * sizeof (WCHAR));
					}
					else if (key2[8] == 0 && i + 1 < numargs)
					{
						value_ptr = arga[++i];
					}

					if (value_ptr)
					{
						LONG instance_id = (LONG)wcstol (value_ptr, NULL, 10);

						if (pbi->instance_id < 1 && instance_id >= 1)
							pbi->instance_id = instance_id;
					}
				}
				else if (_r_str_compare (key2, L"newinstance", 11) == 0)
				{
					if (pbi->instance_id < 1)
						pbi->is_newinstance = TRUE;
				}
				else if (_r_str_compare (key2, L"wait", 4) == 0)
				{
					pbi->is_waitdownloadend = TRUE;
				}
				else if (_r_str_compare (key2, L"update", 6) == 0)
				{
					pbi->is_onlyupdate = TRUE;
				}
				else if (_r_str_compare (key2, L"taskupdate", 10) == 0)
				{
					pbi->is_taskupdate = TRUE;
				}
				else if (*key == L'-')
				{
					if (!pbi->is_opennewwindow)
					{
						if (_r_str_compare (key, L"-new-tab", 8) == 0 ||
							_r_str_compare (key, L"-new-window", 11) == 0 ||
							_r_str_compare (key, L"--new-window", 12) == 0 ||
							_r_str_compare (key, L"-new-instance", 13) == 0)
						{
							pbi->is_opennewwindow = TRUE;
						}
					}

					// there is Chromium arguments
					//_r_str_appendformat (pbi->urls, RTL_NUMBER_OF (pbi->urls), L" %s", key);
				}
			}
			else if (_app_path_is_url (key))
			{
				// there is Chromium url
				pbi->is_hasurls = TRUE;
			}
		}
	}

	if (pbi->is_hasurls)
	{
		pbi->urls_str = _r_obj_createstring (_r_sys_getcommandline () + first_arg_length + 2);

		_r_str_trimstring2 (&pbi->urls_str->sr, L" ", 0);
	}

	LocalFree (arga);
}
