#include "Config.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace
{
	using json = nlohmann::json;

	Config::ActionType ParseType(const std::string& a_type)
	{
		if (a_type == "mcm")
			return Config::ActionType::kMCM;
		if (a_type == "modevent")
			return Config::ActionType::kModEvent;
		if (a_type == "skse_message")
			return Config::ActionType::kSkseMessage;
		return Config::ActionType::kNone;
	}

	void ParseFile(const std::filesystem::path& a_path, int& a_loadCounter)
	{
		std::ifstream stream(a_path);
		if (!stream) {
			logger::warn("STBSP: cannot open {}", a_path.filename().string());
			return;
		}

		json doc;
		try {
			stream >> doc;
		} catch (const std::exception& e) {
			logger::error("STBSP: failed to parse {}: {}", a_path.filename().string(), e.what());
			return;
		}

		if (const auto it = doc.find("hide"); it != doc.end() && it->is_array()) {
			for (const auto& h : *it)
				if (h.is_string())
					Config::hide.insert(h.get<std::string>());
		}

		if (const auto it = doc.find("settingsTabs"); it != doc.end() && it->is_array()) {
			for (const auto& t : *it) {
				if (!t.is_object())
					continue;
				Config::SettingsTab tab;
				tab.id = t.value("id", std::string{});
				tab.text = t.value("text", std::string{});
				tab.vanilla = t.value("vanilla", std::string{});
				tab.priority = t.value("priority", 0);

				if (const auto a = t.find("action"); a != t.end() && a->is_object()) {
					tab.action.type = ParseType(a->value("type", std::string{}));
					tab.action.modName = a->value("modName", std::string{});
					tab.action.page = a->value("page", std::string{});
					tab.action.eventName = a->value("eventName", std::string{});
					tab.action.strArg = a->value("strArg", std::string{});
					tab.action.numArg = a->value("numArg", 0.0f);
					tab.action.messageId = a->value("messageId", std::string{});
				}

				const bool okVanilla = (tab.vanilla == "controls");
				const bool okAction = (tab.vanilla.empty() && tab.action.type != Config::ActionType::kNone);
				if (tab.text.empty() || (!okVanilla && !okAction)) {
					logger::warn("STBSP: skipping settingsTab (empty text, or no valid vanilla/action) in {}",
						a_path.filename().string());
					continue;
				}
				Config::settingsTabs.push_back(std::move(tab));
			}
		}

		if (const auto it = doc.find("entries"); it != doc.end() && it->is_array()) {
			for (const auto& e : *it) {
				if (!e.is_object())
					continue;

				Config::Entry entry;
				entry.id = e.value("id", std::string{});
				entry.text = e.value("text", std::string{});
				entry.priority = e.value("priority", 0);
				entry.loadIndex = a_loadCounter++;

				if (const auto a = e.find("action"); a != e.end() && a->is_object()) {
					entry.action.type = ParseType(a->value("type", std::string{}));
					entry.action.modName = a->value("modName", std::string{});
					entry.action.page = a->value("page", std::string{});
					entry.action.eventName = a->value("eventName", std::string{});
					entry.action.strArg = a->value("strArg", std::string{});
					entry.action.numArg = a->value("numArg", 0.0f);
					entry.action.messageId = a->value("messageId", std::string{});
				}

				if (entry.text.empty() || entry.action.type == Config::ActionType::kNone) {
					logger::warn("STBSP: skipping entry (empty text or unknown action.type) in {}",
						a_path.filename().string());
					continue;
				}
				Config::entries.push_back(std::move(entry));
			}
		}
	}
}

void Config::Load()
{
	entries.clear();
	hide.clear();
	settingsTabs.clear();

	std::error_code ec;
	const std::filesystem::path base{ "Data/SKSE/Plugins/STBSystemPage.json" };
	if (!std::filesystem::exists(base, ec)) {
		logger::warn("STBSP: '{}' not found, no config loaded", base.string());
		return;
	}

	int loadCounter = 0;
	ParseFile(base, loadCounter);

	std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
		if (a.priority != b.priority)
			return a.priority > b.priority;
		return a.loadIndex < b.loadIndex;
	});

	logger::info("STBSP: loaded {} custom entries, {} hidden vanilla items, {} settings tabs",
		entries.size(), hide.size(), settingsTabs.size());
}
