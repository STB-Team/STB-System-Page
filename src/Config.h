#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace Config
{
	enum class ActionType
	{
		kNone = 0,
		kMCM,
		kModEvent,
		kSkseMessage,
	};

	struct Action
	{
		ActionType type{ ActionType::kNone };

		std::string modName;
		std::string page;

		std::string eventName;
		std::string strArg;
		float       numArg{ 0.0f };

		std::string messageId;
	};

	struct Entry
	{
		std::string id;
		std::string text;
		int         priority{ 0 };
		int         loadIndex{ 0 };
		Action      action;
	};

	struct SettingsTab
	{
		std::string id;
		std::string text;
		std::string vanilla;
		Action      action;
		int         priority{ 0 };
	};

	inline std::vector<Entry> entries;
	inline std::unordered_set<std::string> hide;
	inline std::vector<SettingsTab> settingsTabs;

	void Load();
}
