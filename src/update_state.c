// chrlauncher
// Copyright (c) 2015-2025 Henry++

#include "routine.h"

#include "main.h"
#include "rapp.h"

BOOLEAN _app_ishaveupdate (
	_In_ PBROWSER_INFORMATION pbi
)
{
	return !_r_obj_isstringempty (pbi->download_url) && !_r_obj_isstringempty (pbi->new_version);
}

BOOLEAN _app_isupdatedownloaded (
	_In_ PBROWSER_INFORMATION pbi
)
{
	return !_r_obj_isstringempty (pbi->cache_path) && _r_fs_exists (&pbi->cache_path->sr);
}

BOOLEAN _app_isupdaterequired (
	_In_ PBROWSER_INFORMATION pbi
)
{
	LONG64 timestamp;

	if (!_r_fs_exists (&pbi->binary_path->sr))
		return TRUE;

	if (pbi->is_forcecheck)
		return TRUE;

	if (pbi->check_period)
	{
		timestamp = _r_unixtime_now ();
		timestamp -= _r_config_getlong64 (L"ChromiumLastCheck", 0);

		if (timestamp >= _r_calc_days2seconds (pbi->check_period))
			return TRUE;
	}

	return FALSE;
}

ULONG _app_getactionid (
	_In_ PBROWSER_INFORMATION pbi
)
{
	if (_app_isupdatedownloaded (pbi))
	{
		return IDS_ACTION_INSTALL;
	}
	else if (_app_ishaveupdate (pbi))
	{
		return IDS_ACTION_DOWNLOAD;
	}

	return IDS_ACTION_CHECK;
}
