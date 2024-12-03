#include <filesystem>
#include <algorithm>

#include <extension.h>
#include <util/librandom.h>
#include "profile.h"

#undef min
#undef max
#undef clamp

CDifficultyManager::~CDifficultyManager()
{
	m_profiles.clear();
}

void CDifficultyManager::LoadProfiles()
{
	rootconsole->ConsolePrint("Reading bot difficulty profiles!");

	std::unique_ptr<char[]> path = std::make_unique<char[]>(PLATFORM_MAX_PATH);

	const char* modfolder = smutils->GetGameFolderName();
	bool found = false;
	bool is_override = false;

	if (modfolder != nullptr)
	{
		// Loof for mod specific custom file
		smutils->BuildPath(SourceMod::Path_SM, path.get(), PLATFORM_MAX_PATH, "configs/navbot/%s/bot_difficulty.custom.cfg", modfolder);

		if (std::filesystem::exists(path.get()))
		{
			found = true;
			is_override = true;
		}
		else
		{
			// look for mod file
			smutils->BuildPath(SourceMod::Path_SM, path.get(), PLATFORM_MAX_PATH, "configs/navbot/%s/bot_difficulty.cfg", modfolder);

			if (std::filesystem::exists(path.get()))
			{
				found = true;
			}
		}
	}

	if (!found) // file not found on mod specific folder
	{
		smutils->BuildPath(SourceMod::Path_SM, path.get(), PLATFORM_MAX_PATH, "configs/navbot/bot_difficulty.custom.cfg");

		if (std::filesystem::exists(path.get()))
		{
			found = true;
			is_override = true;
		}
		else
		{
			smutils->BuildPath(SourceMod::Path_SM, path.get(), PLATFORM_MAX_PATH, "configs/navbot/bot_difficulty.cfg");

			if (std::filesystem::exists(path.get()))
			{
				found = true;
			}
		}
	}

	if (!found)
	{
		smutils->LogError(myself, "Failed to read bot difficulty profile configuration file at \"%s\"!", path.get());
		return;
	}
	else
	{
		if (is_override)
		{
			smutils->LogMessage(myself, "Parsing bot profile override file at \"%s\".", path.get());
		}
	}

	SourceMod::SMCStates states;
	auto error = textparsers->ParseFile_SMC(path.get(), this, &states);

	if (error != SourceMod::SMCError_Okay)
	{
		smutils->LogError(myself, "Failed to read bot difficulty profile configuration file at \"%s\"!", path.get());
		m_profiles.clear();
		return;
	}

	smutils->LogMessage(myself, "Loaded bot difficulty profiles. Number of profiles: %i", m_profiles.size());
}

std::shared_ptr<DifficultyProfile> CDifficultyManager::GetProfileForSkillLevel(const int level) const
{
	std::vector<std::shared_ptr<DifficultyProfile>> collected;
	collected.reserve(32);

	for (auto& ptr : m_profiles)
	{
		if (ptr->GetSkillLevel() == level)
		{
			collected.push_back(ptr);
		}
	}

	if (collected.size() == 0)
	{
		smutils->LogError(myself, "Difficulty profile for skill level '%i' not found! Using default profile.", level);
		return std::make_shared<DifficultyProfile>();
	}

	return collected[randomgen->GetRandomInt<size_t>(0U, collected.size() - 1U)];
}

void CDifficultyManager::ReadSMC_ParseStart()
{
	m_parser_depth = 0;
	m_current = nullptr;
}

void CDifficultyManager::ReadSMC_ParseEnd(bool halted, bool failed)
{
	m_parser_depth = 0;
	m_current = nullptr;
}

SourceMod::SMCResult CDifficultyManager::ReadSMC_NewSection(const SourceMod::SMCStates* states, const char* name)
{
	if (strncmp(name, "BotDifficultyProfiles", 21) == 0)
	{
		m_parser_depth++;
		return SMCResult_Continue;
	}

	if (m_parser_depth == 1)
	{
		// new profile
		auto& profile = m_profiles.emplace_back(new DifficultyProfile);
		m_current = profile.get();
	}

	if (m_parser_depth == 2)
	{
		if (strncasecmp(name, "custom_data", 11) == 0)
		{
			m_parser_depth++;
			return SMCResult_Continue;
		}
		else
		{
			smutils->LogError(myself, "Unexpected section %s at L %i C %i", name, states->line, states->col);
			return SMCResult_HaltFail;
		}
	}

	// max depth should be 3

	if (m_parser_depth > 3)
	{
		smutils->LogError(myself, "Unexpected section %s at L %i C %i", name, states->line, states->col);
		return SMCResult_HaltFail;
	}

	m_parser_depth++;
	return SMCResult_Continue;
}

SourceMod::SMCResult CDifficultyManager::ReadSMC_KeyValue(const SourceMod::SMCStates* states, const char* key, const char* value)
{
	if (m_parser_depth == 3) // parsing a custom data block
	{
		std::string szKey(key);

		if (m_current->ContainsCustomData(szKey))
		{
			smutils->LogError(myself, "Duplicate custom data %s %s at line %i col %i", key, value, states->line, states->col);
			return SMCResult_Continue;
		}

		m_current->SaveCustomData(szKey, atof(value));

		return SMCResult_Continue;
	}

	if (strncasecmp(key, "skill_level", 11) == 0)
	{
		int v = atoi(value);

		if (v < 0 || v > 255)
		{
			v = 0;
			smutils->LogError(myself, "Skill level should be between 0 and 255! %s <%s> at line %i col %i", key, value, states->line, states->col);
		}

		m_current->SetSkillLevel(v);
	}
	else if (strncasecmp(key, "aimspeed", 8) == 0)
	{
		float v = atof(value);

		if (v < 180.0f)
		{
			v = 500.0f;
			smutils->LogError(myself, "Aim speed cannot be less than 180 degrees per second! %s <%s> at line %i col %i", key, value, states->line, states->col);
		}

		m_current->SetAimSpeed(v);
	}
	else if (strncasecmp(key, "fov", 3) == 0)
	{
		int v = atoi(value);

		if (v < 60 || v > 179)
		{
			v = std::clamp(v, 60, 179);
			smutils->LogError(myself, "FOV should be between 60 and 179! %s <%s> at line %i col %i", key, value, states->line, states->col);
		}

		m_current->SetFOV(v);
	}
	else if (strncasecmp(key, "maxvisionrange", 14) == 0)
	{
		int v = atoi(value);

		if (v < 1024 || v > 16384)
		{
			v = std::clamp(v, 1024, 16384);
			smutils->LogError(myself, "Max vision range should be between 1024 and 16384! %s <%s> at line %i col %i", key, value, states->line, states->col);
		}

		m_current->SetMaxVisionRange(v);
	}
	else if (strncasecmp(key, "maxhearingrange", 15) == 0)
	{
		int v = atoi(value);

		if (v < 256 || v > 16384)
		{
			v = std::clamp(v, 256, 16384);
			smutils->LogError(myself, "Max hearing range should be between 256 and 16384! %s <%s> at line %i col %i", key, value, states->line, states->col);
		}

		m_current->SetMaxHearingRange(v);
	}
	else if (strncasecmp(key, "minrecognitiontime", 18) == 0)
	{
		float v = atof(value);

		if (v < 0.001f || v > 1.0f)
		{
			v = 0.23f;
			smutils->LogError(myself, "Minimum recognition time should be between 0.001 and 1.0! %s <%s> at line %i col %i", key, value, states->line, states->col);
		}

		m_current->SetMinRecognitionTime(v);
	}
	else
	{
		smutils->LogError(myself, "Unknown key \"%s\" with value \"%s\" found while parsing bot difficulty profile!", key, value);
	}

	return SMCResult_Continue;
}

SourceMod::SMCResult CDifficultyManager::ReadSMC_LeavingSection(const SourceMod::SMCStates* states)
{
	if (m_parser_depth == 2)
	{
		m_current = nullptr;
	}

	m_parser_depth--;
	return SMCResult_Continue;
}
