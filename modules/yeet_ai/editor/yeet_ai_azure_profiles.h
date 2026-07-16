/**************************************************************************/
/*  yeet_ai_azure_profiles.h                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/
/*  Multi-profile Azure / Foundry configuration for Crosshair AI.         */
/*  Profiles are stored in EditorSettings as an Array of Dictionaries.    */
/**************************************************************************/

#pragma once

#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/string/ustring.h"

// Profile dictionary keys:
//   id, name, endpoint, deployment, api_key, api_mode (int 0|1), api_version, model

// Load profiles; migrates legacy single azure_* keys into one profile if needed.
Array yeet_ai_azure_profiles_load();

// Active profile id (empty if none).
String yeet_ai_azure_active_profile_id();

// Active profile dict (empty if none). Ensures migration first.
Dictionary yeet_ai_azure_active_profile();

// Find profile by id; empty Dictionary if missing.
Dictionary yeet_ai_azure_profile_by_id(const String &p_id);

// Persist profiles + active id; also mirrors active profile into legacy keys
// so older code paths keep working.
void yeet_ai_azure_profiles_save(const Array &p_profiles, const String &p_active_id);

// Create a new profile dict with unique id and defaults.
Dictionary yeet_ai_azure_make_profile(const String &p_name = String());

// Ensure at least one profile exists (from legacy keys or defaults).
void yeet_ai_azure_profiles_ensure_migrated();
