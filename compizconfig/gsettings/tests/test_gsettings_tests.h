#ifndef _COMPIZCONFIG_TEST_GSETTINGS_TESTS_H
#define _COMPIZCONFIG_TEST_GSETTINGS_TESTS_H

#include <gtest/gtest.h>
#include <glib.h>
#include <glib-object.h>
#include <gsettings-mock-schemas-config.h>

using ::testing::TestWithParam;

class CCSGSettingsTeardownSetupInterface
{
    public:
	virtual void SetUp () = 0;
	virtual void TearDown () = 0;
};

class CCSGSettingsTestingEnv
{
    public:

	virtual void SetUpEnv ()
	{
	    setenv ("G_SLICE", "always-malloc", 1);
	}

	virtual void TearDownEnv ()
	{
	    unsetenv ("G_SLICE");
	}
};

class CCSGSettingsMemoryBackendTestingEnv :
    public CCSGSettingsTestingEnv
{
    public:

	virtual void SetUpEnv ()
	{
	    CCSGSettingsTestingEnv::SetUpEnv ();

	    g_setenv ("GSETTINGS_SCHEMA_DIR", MOCK_PATH.c_str (), true);
	    g_setenv ("GSETTINGS_BACKEND", "memory", 1);
	}

	virtual void TearDownEnv ()
	{
	    g_unsetenv ("GSETTINGS_BACKEND");
	    g_unsetenv ("GSETTINGS_SCHEMA_DIR");

	    CCSGSettingsTestingEnv::TearDownEnv ();
	}
};

class CCSGSettingsTest :
    public CCSGSettingsTestingEnv,
    public ::testing::TestWithParam <CCSGSettingsTeardownSetupInterface *>
{
    public:

	CCSGSettingsTest () :
	    mFuncs (GetParam ())
	{
	}

	virtual void SetUp ()
	{
	    CCSGSettingsTestingEnv::SetUpEnv ();
	    mFuncs->SetUp ();
	}

	virtual void TearDown ()
	{
	    CCSGSettingsTestingEnv::TearDownEnv ();
	    mFuncs->TearDown ();
	}

    private:

	CCSGSettingsTeardownSetupInterface *mFuncs;
};

class CCSGSettingsTestIndependent :
    public CCSGSettingsTestingEnv,
    public ::testing::Test
{
    public:

	virtual void SetUp ()
	{
	    g_type_init ();

	    CCSGSettingsTestingEnv::SetUpEnv ();
	}

	virtual void TearDown ()
	{
	    CCSGSettingsTestingEnv::TearDownEnv ();
	}
};

class CCSGSettingsTestWithMemoryBackend :
    public CCSGSettingsTestIndependent,
    public CCSGSettingsMemoryBackendTestingEnv
{
    public:

	virtual void SetUp ()
	{
	    CCSGSettingsTestIndependent::SetUp ();
	    CCSGSettingsMemoryBackendTestingEnv::SetUpEnv ();
	}

	virtual void TearDown ()
	{
	    CCSGSettingsMemoryBackendTestingEnv::TearDownEnv ();
	    CCSGSettingsTestIndependent::TearDown ();
	}
};

#endif
