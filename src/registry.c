// chrlauncher
// Copyright (c) 2015-2025 Henry++

#include "routine.h"

#include "main.h"
#include "rapp.h"

static BOOLEAN _app_reg_read_string (
	_In_ HKEY hkey,
	_In_opt_ LPCWSTR subkey,
	_In_opt_ LPCWSTR value_name,
	_Out_ PR_STRING* string_ptr
)
{
	HKEY key = NULL;
	LSTATUS status;
	ULONG type;
	ULONG size = 0;
	PR_STRING result;

	status = RegOpenKeyExW (hkey, subkey, 0, KEY_READ, &key);

	if (status != ERROR_SUCCESS)
		return FALSE;

	status = RegQueryValueExW (key, value_name, NULL, &type, NULL, &size);

	if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA)
	{
		RegCloseKey (key);
		return FALSE;
	}

	if (type != REG_SZ && type != REG_EXPAND_SZ)
	{
		RegCloseKey (key);
		return FALSE;
	}

	result = _r_obj_createstring_ex (NULL, size + sizeof (WCHAR));

	status = RegQueryValueExW (key, value_name, NULL, &type, (LPBYTE)result->buffer, &size);

	if (status != ERROR_SUCCESS)
	{
		_r_obj_dereference (result);
		RegCloseKey (key);
		return FALSE;
	}

	// ensure null-termination
	result->buffer[size / sizeof (WCHAR)] = L'\0';
	result->length = size;

	_r_str_trimtonullterminator (&result->sr);

	RegCloseKey (key);

	*string_ptr = result;

	return TRUE;
}

static BOOLEAN _app_reg_write_string (
	_In_ HKEY hkey,
	_In_opt_ LPCWSTR subkey,
	_In_opt_ LPCWSTR value_name,
	_In_ LPCWSTR data
)
{
	HKEY key = NULL;
	LSTATUS status;
	ULONG disposition;
	ULONG data_size;

	status = RegOpenKeyExW (hkey, subkey, 0, KEY_SET_VALUE, &key);

	if (status != ERROR_SUCCESS)
	{
		status = RegCreateKeyExW (hkey, subkey, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, &disposition);

		if (status != ERROR_SUCCESS)
			return FALSE;
	}

	data_size = (ULONG)((wcslen (data) + 1) * sizeof (WCHAR));

	status = RegSetValueExW (key, value_name, 0, REG_SZ, (const BYTE*)data, data_size);

	RegCloseKey (key);

	return (status == ERROR_SUCCESS);
}

static PR_STRING _app_insert_profile_dir_in_command (
	_In_ PR_STRING command,
	_In_ PR_STRING profile_dir
)
{
	static const WCHAR SINGLE_ARGUMENT[] = L"--single-argument";
	static const WCHAR USER_DATA_DIR[] = L"--user-data-dir";

	PR_STRING profile_arg;
	PR_STRING result;
	LPCWSTR insert_pos;
	LPCWSTR cmd_end;
	SIZE_T prefix_len;
	SIZE_T suffix_len;
	SIZE_T result_len;

	// check if --user-data-dir is already present
	if (StrStrIW (command->buffer, USER_DATA_DIR))
		return NULL; // already patched

	profile_arg = _r_format_string (L"--user-data-dir=\"%s\"", profile_dir->buffer);

	if (!profile_arg)
		return NULL;

	// find --single-argument to insert before it
	insert_pos = StrStrIW (command->buffer, SINGLE_ARGUMENT);

	if (insert_pos)
	{
		// insert before --single-argument
		// skip back over any whitespace before --single-argument
		while (insert_pos > command->buffer && *(insert_pos - 1) == L' ')
			insert_pos--;

		prefix_len = (SIZE_T)(insert_pos - command->buffer) * sizeof (WCHAR);
		suffix_len = (SIZE_T)command->length - prefix_len;

		result_len = prefix_len + (SIZE_T)profile_arg->length + sizeof (WCHAR) + suffix_len;

		result = _r_obj_createstring_ex (NULL, result_len);

		if (result)
		{
			SIZE_T offset = 0;

			if (prefix_len)
			{
				RtlCopyMemory (result->buffer, command->buffer, prefix_len);
				offset += prefix_len;

				RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, offset), L" ", sizeof (WCHAR));
				offset += sizeof (WCHAR);
			}

			RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, offset), profile_arg->buffer, profile_arg->length);
			offset += profile_arg->length;

			RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, offset), L" ", sizeof (WCHAR));
			offset += sizeof (WCHAR);

			RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, offset), insert_pos, suffix_len);

			_r_str_trimtonullterminator (&result->sr);
		}
	}
	else
	{
		// no --single-argument found; insert before -- "%1" or at end
		static const WCHAR URL_TOKEN[] = L"-- \"%1";
		LPCWSTR url_pos = StrStrIW (command->buffer, URL_TOKEN);

		if (url_pos)
		{
			while (url_pos > command->buffer && *(url_pos - 1) == L' ')
				url_pos--;

			prefix_len = (SIZE_T)(url_pos - command->buffer) * sizeof (WCHAR);
			suffix_len = (SIZE_T)command->length - prefix_len;

			result_len = prefix_len + (SIZE_T)profile_arg->length + sizeof (WCHAR) + suffix_len;

			result = _r_obj_createstring_ex (NULL, result_len);

			if (result)
			{
				SIZE_T offset = 0;

				if (prefix_len)
				{
					RtlCopyMemory (result->buffer, command->buffer, prefix_len);
					offset += prefix_len;

					RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, offset), L" ", sizeof (WCHAR));
					offset += sizeof (WCHAR);
				}

				RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, offset), profile_arg->buffer, profile_arg->length);
				offset += profile_arg->length;

				RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, offset), L" ", sizeof (WCHAR));
				offset += sizeof (WCHAR);

				RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, offset), url_pos, suffix_len);

				_r_str_trimtonullterminator (&result->sr);
			}
		}
		else
		{
			// append at end
			result_len = (SIZE_T)command->length + sizeof (WCHAR) + (SIZE_T)profile_arg->length;

			result = _r_obj_createstring_ex (NULL, result_len);

			if (result)
			{
				RtlCopyMemory (result->buffer, command->buffer, command->length);
				RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, command->length), L" ", sizeof (WCHAR));
				RtlCopyMemory (PTR_ADD_OFFSET (result->buffer, command->length + sizeof (WCHAR)), profile_arg->buffer, profile_arg->length);

				_r_str_trimtonullterminator (&result->sr);
			}
		}
	}

	_r_obj_dereference (profile_arg);

	return result;
}

BOOLEAN _app_patch_registry_profile (
	_In_ PBROWSER_INFORMATION pbi
)
{
	PR_STRING prog_id = NULL;
	PR_STRING command = NULL;
	PR_STRING new_command = NULL;
	BOOLEAN is_success = FALSE;

	if (!pbi || _r_obj_isstringempty (pbi->profile_dir))
		return FALSE;

	// read ProgId from UserChoice for http
	if (!_app_reg_read_string (HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\http\\UserChoice", L"ProgId", &prog_id))
		return FALSE;

	// read open command from the ProgId
	if (!_app_reg_read_string (HKEY_CLASSES_ROOT, prog_id->buffer, L"shell\\open\\command", &command))
	{
		// try HKCU\Software\Classes as fallback
		if (!_app_reg_read_string (HKEY_CURRENT_USER, prog_id->buffer, L"shell\\open\\command", &command))
		{
			_r_obj_dereference (prog_id);
			return FALSE;
		}
	}

	// insert profile dir
	new_command = _app_insert_profile_dir_in_command (command, pbi->profile_dir);

	if (new_command)
	{
		// write back - try HKCR first, then HKCU\Software\Classes
		if (_app_reg_write_string (HKEY_CLASSES_ROOT, prog_id->buffer, L"shell\\open\\command", new_command->buffer))
		{
			is_success = TRUE;
		}
		else
		{
			// fallback to HKCU
			PR_STRING subkey = _r_format_string (L"Software\\Classes\\%s\\shell\\open\\command", prog_id->buffer);

			if (subkey)
			{
				is_success = _app_reg_write_string (HKEY_CURRENT_USER, subkey->buffer, NULL, new_command->buffer);
				_r_obj_dereference (subkey);
			}
		}

		_r_obj_dereference (new_command);
	}

	// also patch https
	{
		PR_STRING https_prog_id = NULL;
		PR_STRING https_command = NULL;
		PR_STRING https_new_command = NULL;

		if (_app_reg_read_string (HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\https\\UserChoice", L"ProgId", &https_prog_id))
		{
			// only patch if different ProgId or same
			if (_app_reg_read_string (HKEY_CLASSES_ROOT, https_prog_id->buffer, L"shell\\open\\command", &https_command))
			{
				https_new_command = _app_insert_profile_dir_in_command (https_command, pbi->profile_dir);

				if (https_new_command)
				{
					if (!_app_reg_write_string (HKEY_CLASSES_ROOT, https_prog_id->buffer, L"shell\\open\\command", https_new_command->buffer))
					{
						PR_STRING subkey = _r_format_string (L"Software\\Classes\\%s\\shell\\open\\command", https_prog_id->buffer);

						if (subkey)
						{
							_app_reg_write_string (HKEY_CURRENT_USER, subkey->buffer, NULL, https_new_command->buffer);
							_r_obj_dereference (subkey);
						}
					}

					_r_obj_dereference (https_new_command);
				}

				_r_obj_dereference (https_command);
			}

			_r_obj_dereference (https_prog_id);
		}
	}

	_r_obj_dereference (command);
	_r_obj_dereference (prog_id);

	return is_success;
}

BOOLEAN _app_is_registry_patched (
	_In_ PBROWSER_INFORMATION pbi
)
{
	PR_STRING prog_id = NULL;
	PR_STRING command = NULL;
	BOOLEAN is_patched = FALSE;

	if (!pbi || _r_obj_isstringempty (pbi->profile_dir))
		return FALSE;

	if (!_app_reg_read_string (HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\http\\UserChoice", L"ProgId", &prog_id))
		return FALSE;

	if (_app_reg_read_string (HKEY_CLASSES_ROOT, prog_id->buffer, L"shell\\open\\command", &command))
	{
		is_patched = (StrStrIW (command->buffer, L"--user-data-dir") != NULL);
		_r_obj_dereference (command);
	}

	if (!is_patched)
	{
		if (_app_reg_read_string (HKEY_CURRENT_USER, prog_id->buffer, L"shell\\open\\command", &command))
		{
			is_patched = (StrStrIW (command->buffer, L"--user-data-dir") != NULL);
			_r_obj_dereference (command);
		}
	}

	_r_obj_dereference (prog_id);

	return is_patched;
}
