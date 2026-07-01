#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

using namespace RE;
#include <algorithm>
#include <xbyak/xbyak.h>

#include "Config.h"
#include "STBSystemPageAPI.h"


namespace SystemMenuButton
{
	constexpr const char* kMenuRootPath = "_root.QuestJournalFader.Menu_mc";

	std::atomic<bool> g_injected{ false };
	std::atomic<int>  g_injectTicks{ 0 };
	std::atomic<int>  g_pendingEntry{ -1 };
	std::atomic<int>  g_selectTicks{ 0 };
	std::string       g_selectTarget;

	std::atomic<int>  g_catWatchTicks{ 0 };
	std::atomic<int>  g_catLastSize{ -1 };

	using Predicate = bool (*)(const RE::GFxValue&);

	bool FindByPredicate(const RE::GFxValue& a_root, Predicate a_pred, RE::GFxValue& a_out, int a_maxDepth)
	{
		std::vector<RE::GFxValue> cur{ a_root };
		int budget = 2500;
		for (int depth = 0; depth <= a_maxDepth && !cur.empty(); ++depth) {
			std::vector<RE::GFxValue> next;
			for (auto& node : cur) {
				if (--budget <= 0)
					return false;
				if (a_pred(node)) {
					a_out = node;
					return true;
				}
				if (depth == a_maxDepth)
					continue;
				node.VisitMembers([&next](const char* a_name, const RE::GFxValue& a_val) {
					static constexpr std::string_view skip[] = { "_parent", "_root", "_global", "_level0", "__proto__",
						"prototype", "constructor", "_listeners", "stage" };
					for (auto s : skip)
						if (s == a_name)
							return;
					if (a_val.IsObject() || a_val.IsArray() || a_val.IsDisplayObject())
						next.push_back(a_val);
				});
			}
			cur.swap(next);
		}
		return false;
	}

	bool IsSystemPage(const RE::GFxValue& v)
	{
		return v.IsObject() && v.HasMember("CategoryList") && v.HasMember("SettingsList") && v.HasMember("MappingList");
	}

	bool IsConfigPanel(const RE::GFxValue& v)
	{
		return v.IsObject() && v.HasMember("_modList") && v.HasMember("_optionsList") && v.HasMember("_modListPanel");
	}

	class PressHandler : public RE::GFxFunctionHandler
	{
	public:
		void Call(Params& a_params) override
		{
			if (a_params.argCount < 1 || !a_params.args)
				return;
			RE::GFxValue& evt = a_params.args[0];

			RE::GFxValue entry, eidx;
			if (evt.GetMember("entry", &entry) && entry.GetMember("__stb_eidx", &eidx) && eidx.IsNumber()) {
				g_pendingEntry.store(static_cast<int>(eidx.GetNumber()));
				return;
			}

			RE::GFxValue vidx;
			const bool hasVidx = entry.GetMember("__stb_vidx", &vidx) && vidx.IsNumber();

			if (!a_params.thisPtr)
				return;
			RE::GFxValue page;
			if (!a_params.thisPtr->GetMember("__stb_page", &page))
				return;
			if (!page.HasMember("IDX_QUIT") && hasVidx)
				evt.SetMember("index", vidx);

			page.Invoke("onCategoryButtonPress", nullptr, &evt, 1);
		}
	};
	PressHandler g_pressHandler;

	void DispatchAction(RE::GFxMovieView* a_view, const Config::Action& a_action, const std::string& a_id);

	class SettingsPressHandler : public RE::GFxFunctionHandler
	{
	public:
		void Call(Params& a_params) override
		{
			if (a_params.argCount < 1 || !a_params.args || !a_params.thisPtr)
				return;
			RE::GFxValue& evt = a_params.args[0];
			RE::GFxValue page;
			if (!a_params.thisPtr->GetMember("__stb_page", &page))
				return;

			RE::GFxValue entry, sidx;
			if (evt.GetMember("entry", &entry) && entry.GetMember("__stb_stidx", &sidx) && sidx.IsNumber()) {
				const int idx = static_cast<int>(sidx.GetNumber());
				if (idx < 0 || idx >= static_cast<int>(Config::settingsTabs.size()))
					return;
				const auto& tab = Config::settingsTabs[idx];

				if (tab.vanilla == "controls") {
					RE::GFxValue mappingList, mEntryList, lenV;
					if (page.GetMember("MappingList", &mappingList) &&
						mappingList.GetMember("entryList", &mEntryList) &&
						(!mEntryList.GetMember("length", &lenV) || !lenV.IsNumber() || lenV.GetNumber() == 0.0)) {
						if (auto* view = static_cast<RE::GFxMovieView*>(a_params.movie)) {
							RE::GFxValue gameDelegate;
							if (view->GetVariable(&gameDelegate, "_global.gfx.io.GameDelegate")) {
								RE::GFxValue argArr;
								view->CreateArray(&argArr);
								argArr.PushBack(mEntryList);
								const RE::GFxValue callArgs[2] = { RE::GFxValue("RequestInputMappings"), argArr };
								gameDelegate.Invoke("call", nullptr, callArgs, 2);
							}
						}
						RE::GFxValue sortFn;
						if (page.GetMember("inputMappingSort", &sortFn))
							mEntryList.Invoke("sort", nullptr, &sortFn, 1);
						mappingList.Invoke("InvalidateData");
					}
					RE::GFxValue settingsPanel;
					if (page.GetMember("SettingsPanel", &settingsPanel))
						settingsPanel.SetMember("bCloseToMainState", RE::GFxValue(false));
					page.Invoke("EndState");
					const RE::GFxValue st{ static_cast<double>(6) };
					page.Invoke("StartState", nullptr, &st, 1);
				} else {
					DispatchAction(static_cast<RE::GFxMovieView*>(a_params.movie), tab.action, tab.id);
				}
				return;
			}

			RE::GFxValue svidx;
			if (entry.GetMember("__stb_svidx", &svidx) && svidx.IsNumber()) {
				RE::GFxValue settingsList;
				if (page.GetMember("SettingsList", &settingsList))
					settingsList.SetMember("iSelectedIndex", svidx);
			}
			page.Invoke("onSettingsCategoryPress", nullptr, &evt, 1);
		}
	};
	SettingsPressHandler g_settingsPressHandler;

	class EntryPressHandler : public RE::GFxFunctionHandler
	{
	public:
		void Call(Params& a_params) override
		{
			if (!a_params.thisPtr)
				return;
			RE::GFxValue itemIndex, parent;
			if (!a_params.thisPtr->GetMember("itemIndex", &itemIndex) || itemIndex.IsUndefined())
				return;
			if (!a_params.thisPtr->GetMember("_parent", &parent))
				return;
			RE::GFxValue kbOrMouse{ 0.0 };
			if (a_params.argCount >= 2 && a_params.args)
				kbOrMouse = a_params.args[1];
			parent.Invoke("onItemPress", nullptr, &kbOrMouse, 1);
		}
	};
	EntryPressHandler g_entryPressHandler;

	class EntryRollOverHandler : public RE::GFxFunctionHandler
	{
	public:
		void Call(Params& a_params) override
		{
			if (!a_params.thisPtr)
				return;
			RE::GFxValue itemIndex, parent;
			if (!a_params.thisPtr->GetMember("itemIndex", &itemIndex) || itemIndex.IsUndefined())
				return;
			if (!a_params.thisPtr->GetMember("_parent", &parent))
				return;
			RE::GFxValue anim, dis;
			const bool animating = parent.GetMember("listAnimating", &anim) && anim.IsBool() && anim.GetBool();
			const bool disabled = parent.GetMember("bDisableInput", &dis) && dis.IsBool() && dis.GetBool();
			if (animating || disabled)
				return;
			const RE::GFxValue args2[2] = { itemIndex, RE::GFxValue(0.0) };
			parent.Invoke("doSetSelectedIndex", nullptr, args2, 2);
			parent.SetMember("bMouseDrivenNav", RE::GFxValue(true));
		}
	};
	EntryRollOverHandler g_entryRollOverHandler;

	class SafeRefreshHandler : public RE::GFxFunctionHandler
	{
	public:
		void Call(Params& a_params) override
		{
			if (!a_params.thisPtr)
				return;
			RE::GFxValue catList;
			if (a_params.thisPtr->GetMember("CategoryList", &catList))
				catList.Invoke("UpdateList");
		}
	};
	SafeRefreshHandler g_safeRefreshHandler;

	void AddListener(RE::GFxMovieView* a_view, RE::GFxValue& a_catList, RE::GFxValue& a_page)
	{
		RE::GFxValue scope;
		a_view->CreateObject(&scope);
		RE::GFxValue fn;
		a_view->CreateFunction(&fn, &g_pressHandler);
		scope.SetMember("onSTBPress", fn);
		scope.SetMember("__stb_page", a_page);

		const RE::GFxValue rm[3] = { RE::GFxValue("itemPress"), a_page, RE::GFxValue("onCategoryButtonPress") };
		a_catList.Invoke("removeEventListener", nullptr, rm, 3);
		const RE::GFxValue add[3] = { RE::GFxValue("itemPress"), scope, RE::GFxValue("onSTBPress") };
		a_catList.Invoke("addEventListener", nullptr, add, 3);

		a_catList.SetMember("__stb_scope", scope);
		a_catList.SetMember("__stb_injected", RE::GFxValue(1.0));
	}

	void AddSettingsListener(RE::GFxMovieView* a_view, RE::GFxValue& a_list, RE::GFxValue& a_page)
	{
		RE::GFxValue scope;
		a_view->CreateObject(&scope);
		RE::GFxValue fn;
		a_view->CreateFunction(&fn, &g_settingsPressHandler);
		scope.SetMember("onSTBSettingsPress", fn);
		scope.SetMember("__stb_page", a_page);

		const RE::GFxValue rm[3] = { RE::GFxValue("itemPress"), a_page, RE::GFxValue("onSettingsCategoryPress") };
		a_list.Invoke("removeEventListener", nullptr, rm, 3);
		const RE::GFxValue add[3] = { RE::GFxValue("itemPress"), scope, RE::GFxValue("onSTBSettingsPress") };
		a_list.Invoke("addEventListener", nullptr, add, 3);

		a_list.SetMember("__stb_set_scope", scope);
		a_list.SetMember("__stb_set_injected", RE::GFxValue(1.0));
	}

	void EnsureRowClips(RE::GFxMovieView* a_view, RE::GFxValue& a_list, std::uint32_t a_needed, bool a_growHeight)
	{
		RE::GFxValue maxShownV;
		std::uint32_t curClips = 0;
		if (a_list.GetMember("iMaxItemsShown", &maxShownV) && maxShownV.IsNumber())
			curClips = static_cast<std::uint32_t>(maxShownV.GetNumber());
		if (curClips == 0 || a_needed <= curClips)
			return;

		double entryH = 0.0;
		RE::GFxValue e0, h;
		if (a_list.GetMember("Entry0", &e0) && e0.GetMember("_height", &h) && h.IsNumber())
			entryH = h.GetNumber();

		for (std::uint32_t i = curClips; i < a_needed; ++i) {
			const std::string srcName = "Entry" + std::to_string(i - 1);
			RE::GFxValue src;
			if (!a_list.GetMember(srcName.c_str(), &src))
				break;
			const std::string newName = "Entry" + std::to_string(i);
			const RE::GFxValue dupArgs[2] = { RE::GFxValue(newName.c_str()),
				RE::GFxValue(static_cast<double>(20000 + i)) };
			src.Invoke("duplicateMovieClip", nullptr, dupArgs, 2);

			RE::GFxValue nc;
			if (!a_list.GetMember(newName.c_str(), &nc))
				continue;
			nc.SetMember("clipIndex", RE::GFxValue(static_cast<double>(i)));
			RE::GFxValue fnPress, fnRoll;
			a_view->CreateFunction(&fnPress, &g_entryPressHandler);
			a_view->CreateFunction(&fnRoll, &g_entryRollOverHandler);
			nc.SetMember("onPress", fnPress);
			nc.SetMember("onRollOver", fnRoll);
		}

		a_list.SetMember("iMaxItemsShown", RE::GFxValue(static_cast<double>(a_needed)));
		if (a_growHeight && entryH > 0.0) {
			RE::GFxValue border;
			if (a_list.GetMember("border", &border))
				border.SetMember("_height", RE::GFxValue(entryH * static_cast<double>(a_needed) + 4.0));
		}
	}

	bool ApplyHide(RE::GFxValue& a_entryList)
	{
		bool removed = false;
		for (int i = static_cast<int>(a_entryList.GetArraySize()) - 1; i >= 0; --i) {
			RE::GFxValue e, t;
			if (a_entryList.GetElement(static_cast<std::uint32_t>(i), &e) && e.GetMember("text", &t) && t.IsString() &&
				Config::hide.contains(std::string(t.GetString()))) {
				const RE::GFxValue sp[2] = { RE::GFxValue(static_cast<double>(i)), RE::GFxValue(1.0) };
				a_entryList.Invoke("splice", nullptr, sp, 2);
				removed = true;
			}
		}
		return removed;
	}

	void ResetAndUpdateIndices(RE::GFxValue& a_page)
	{
		if (!a_page.HasMember("IDX_QUIT"))
			return;
		static constexpr const char* kIdxFields[] = {
			"IDX_QUICKSAVE", "IDX_SAVE", "IDX_LOAD", "IDX_INSTALLED_CONTENT",
			"IDX_CREATIONS", "IDX_SETTINGS", "IDX_MOD_CONFIG", "IDX_CONTROLS",
			"IDX_HELP", "IDX_QUIT"
		};
		for (auto* f : kIdxFields)
			a_page.SetMember(f, RE::GFxValue(-1.0));
		a_page.Invoke("UpdateIndices");
	}

	bool InjectCategory(RE::GFxMovieView* a_view, RE::GFxValue& page)
	{
		RE::GFxValue catList;
		if (!page.GetMember("CategoryList", &catList) || !catList.IsObject())
			return false;

		RE::GFxValue entryList;
		if (!catList.GetMember("entryList", &entryList) || !entryList.IsArray())
			return false;
		if (entryList.GetArraySize() == 0)
			return false;

		if (catList.HasMember("__stb_injected"))
			return true;

		{
			static const std::unordered_set<std::string_view> kBaseTexts{
				"$SAVE", "$LOAD", "$SETTINGS", "$CONTROLS", "$HELP", "$QUIT"
			};
			const auto count = entryList.GetArraySize();
			bool ready = false;
			for (std::uint32_t i = 0; i < count; ++i) {
				RE::GFxValue e, t;
				if (entryList.GetElement(i, &e) && e.GetMember("text", &t) && t.IsString() &&
					!kBaseTexts.contains(std::string_view(t.GetString()))) {
					ready = true;
					break;
				}
			}
			if (!ready)
				return false;
		}

		const auto vanillaCount = entryList.GetArraySize();
		for (std::uint32_t i = 0; i < vanillaCount; ++i) {
			RE::GFxValue e;
			if (entryList.GetElement(i, &e))
				e.SetMember("__stb_vidx", RE::GFxValue(static_cast<double>(i)));
		}

		ApplyHide(entryList);

		struct Item
		{
			int          prio;
			RE::GFxValue val;
		};
		const auto remaining = entryList.GetArraySize();
		std::vector<Item> items;
		items.reserve(remaining + Config::entries.size());

		for (std::size_t k = 0; k < Config::entries.size(); ++k) {
			RE::GFxValue entry;
			a_view->CreateObject(&entry);
			entry.SetMember("text", RE::GFxValue(Config::entries[k].text.c_str()));
			entry.SetMember("__stb_eidx", RE::GFxValue(static_cast<double>(k)));
			items.push_back({ Config::entries[k].priority, entry });
		}
		for (std::uint32_t i = 0; i < remaining; ++i) {
			RE::GFxValue e;
			if (entryList.GetElement(i, &e))
				items.push_back({ static_cast<int>(remaining - i), e });
		}

		std::stable_sort(items.begin(), items.end(),
			[](const Item& a, const Item& b) { return a.prio > b.prio; });

		const RE::GFxValue clr[2] = { RE::GFxValue(0.0), RE::GFxValue(static_cast<double>(entryList.GetArraySize())) };
		entryList.Invoke("splice", nullptr, clr, 2);
		for (auto& it : items)
			entryList.Invoke("push", nullptr, &it.val, 1);

		EnsureRowClips(a_view, catList, entryList.GetArraySize(), false);

		catList.Invoke("InvalidateData");

		ResetAndUpdateIndices(page);

		g_catLastSize.store(static_cast<int>(entryList.GetArraySize()));
		g_catWatchTicks.store(150);

		AddListener(a_view, catList, page);

		RE::GFxValue safeFn;
		a_view->CreateFunction(&safeFn, &g_safeRefreshHandler);
		page.SetMember("RefreshSystemButtons", safeFn);
		page.SetMember("UpdatePermissions", safeFn);

		logger::info("SystemMenuButton: injected {} custom entries, hid {} vanilla items",
			Config::entries.size(), Config::hide.size());
		return true;
	}

	bool InjectSettings(RE::GFxMovieView* a_view, RE::GFxValue& page)
	{
		RE::GFxValue list;
		if (!page.GetMember("SettingsList", &list) || !list.IsObject())
			return false;

		RE::GFxValue entryList;
		if (!list.GetMember("entryList", &entryList) || !entryList.IsArray())
			return false;
		if (entryList.GetArraySize() == 0)
			return false;

		if (list.HasMember("__stb_set_injected"))
			return true;

		const auto vanillaCount = entryList.GetArraySize();
		for (std::uint32_t i = 0; i < vanillaCount; ++i) {
			RE::GFxValue e;
			if (entryList.GetElement(i, &e))
				e.SetMember("__stb_svidx", RE::GFxValue(static_cast<double>(i)));
		}

		struct Item
		{
			int          prio;
			RE::GFxValue val;
		};
		std::vector<Item> items;
		items.reserve(vanillaCount + Config::settingsTabs.size());

		for (std::size_t k = 0; k < Config::settingsTabs.size(); ++k) {
			RE::GFxValue entry;
			a_view->CreateObject(&entry);
			entry.SetMember("text", RE::GFxValue(Config::settingsTabs[k].text.c_str()));
			entry.SetMember("__stb_stidx", RE::GFxValue(static_cast<double>(k)));
			items.push_back({ Config::settingsTabs[k].priority, entry });
		}
		for (std::uint32_t i = 0; i < vanillaCount; ++i) {
			RE::GFxValue e;
			if (entryList.GetElement(i, &e))
				items.push_back({ static_cast<int>(vanillaCount - i), e });
		}

		std::stable_sort(items.begin(), items.end(),
			[](const Item& a, const Item& b) { return a.prio > b.prio; });

		const RE::GFxValue clr[2] = { RE::GFxValue(0.0), RE::GFxValue(static_cast<double>(entryList.GetArraySize())) };
		entryList.Invoke("splice", nullptr, clr, 2);
		for (auto& it : items)
			entryList.Invoke("push", nullptr, &it.val, 1);

		const std::uint32_t total = entryList.GetArraySize();
		EnsureRowClips(a_view, list, total, true);

		list.Invoke("InvalidateData");

		AddSettingsListener(a_view, list, page);
		logger::info("SystemMenuButton: injected {} settings tab(s); list total {}",
			Config::settingsTabs.size(), total);
		return true;
	}

	bool TryInject(RE::GFxMovieView* a_view)
	{
		const bool needCat = !Config::entries.empty() || !Config::hide.empty();
		const bool needSet = !Config::settingsTabs.empty();
		if (!needCat && !needSet)
			return true;

		RE::GFxValue root;
		if (!a_view->GetVariable(&root, kMenuRootPath))
			return false;

		RE::GFxValue page;
		if (!FindByPredicate(root, IsSystemPage, page, 10))
			return false;

		if (needCat && !InjectCategory(a_view, page))
			return false;
		if (needSet && !InjectSettings(a_view, page))
			return false;
		return true;
	}

	bool TrySelectMod(RE::GFxMovieView* a_view)
	{
		if (g_selectTarget.empty())
			return true;

		RE::GFxValue root;
		if (!a_view->GetVariable(&root, kMenuRootPath))
			return false;

		RE::GFxValue cfg;
		if (!FindByPredicate(root, IsConfigPanel, cfg, 12))
			return false;

		RE::GFxValue state;
		if (cfg.GetMember("_state", &state) && state.IsNumber() && state.GetNumber() != 0.0)
			return false;

		RE::GFxValue modList;
		if (!cfg.GetMember("_modList", &modList))
			return false;

		RE::GFxValue entryList;
		if (!modList.GetMember("entryList", &entryList) || !entryList.IsArray())
			return false;

		auto n = entryList.GetArraySize();
		if (n == 0)
			return false;

		for (std::uint32_t i = 0; i < n; ++i) {
			RE::GFxValue e, mn;
			if (entryList.GetElement(i, &e) && e.GetMember("modName", &mn) && mn.IsString() &&
				g_selectTarget == std::string_view(mn.GetString())) {
				modList.SetMember("selectedIndex", RE::GFxValue(static_cast<double>(i)));
				cfg.Invoke("selectMod", nullptr, &e, 1);
				logger::info("SystemMenuButton: mod '{}' selected in MCM (list pos {})", g_selectTarget, i);
				return true;
			}
		}
		return false;
	}

	void SendModEvent(const Config::Action& a_action)
	{
		auto* source = SKSE::GetModCallbackEventSource();
		if (!source)
			return;
		SKSE::ModCallbackEvent ev{};
		ev.eventName = a_action.eventName.c_str();
		ev.strArg = a_action.strArg.c_str();
		ev.numArg = a_action.numArg;
		ev.sender = nullptr;
		source->SendEvent(&ev);
		logger::info("SystemMenuButton: sent mod event '{}' (str='{}', num={})",
			a_action.eventName, a_action.strArg, a_action.numArg);
	}

	void DispatchSkseMessage(const std::string& a_id, const Config::Action& a_action)
	{
		auto* messaging = SKSE::GetMessagingInterface();
		if (!messaging)
			return;
		STB::SystemPage::ActionMessage msg{ a_id.c_str(), a_action.messageId.c_str() };
		messaging->Dispatch(STB::SystemPage::kMessage_MenuAction, &msg, sizeof(msg), nullptr);
		logger::info("SystemMenuButton: dispatched SKSE message (id='{}', messageId='{}')",
			a_id, a_action.messageId);
	}

	void DispatchAction(RE::GFxMovieView* a_view, const Config::Action& a_action, const std::string& a_id)
	{
		switch (a_action.type) {
		case Config::ActionType::kMCM:
			{
				RE::GFxValue menu;
				if (a_view && a_view->GetVariable(&menu, kMenuRootPath))
					menu.Invoke("ConfigPanelOpen");
				g_selectTarget = a_action.modName;
				g_selectTicks.store(180);
			}
			break;
		case Config::ActionType::kModEvent:
			SendModEvent(a_action);
			break;
		case Config::ActionType::kSkseMessage:
			DispatchSkseMessage(a_id, a_action);
			break;
		default:
			break;
		}
	}

	void DispatchEntry(RE::GFxMovieView* a_view, const Config::Entry& a_entry)
	{
		DispatchAction(a_view, a_entry.action, a_entry.id);
	}

	void WatchCategory(RE::GFxMovieView* a_view)
	{
		if (g_catWatchTicks.load() <= 0)
			return;
		g_catWatchTicks.fetch_sub(1);

		RE::GFxValue root, page;
		if (!a_view->GetVariable(&root, kMenuRootPath) || !FindByPredicate(root, IsSystemPage, page, 10))
			return;
		RE::GFxValue catList, entryList;
		if (!page.GetMember("CategoryList", &catList) ||
			!catList.GetMember("entryList", &entryList) || !entryList.IsArray())
			return;

		const int size = static_cast<int>(entryList.GetArraySize());
		if (size == g_catLastSize.load())
			return;

		ApplyHide(entryList);
		g_catLastSize.store(static_cast<int>(entryList.GetArraySize()));
		ResetAndUpdateIndices(page);
		catList.Invoke("InvalidateData");
	}

	void Tick(RE::JournalMenu* a_this)
	{
		if (!a_this || !a_this->uiMovie)
			return;
		auto* view = a_this->uiMovie.get();

		if (!g_injected.load()) {
			if (g_injectTicks.load() > 0) {
				g_injectTicks.fetch_sub(1);
				if (TryInject(view))
					g_injected.store(true);
			}
			return;
		}

		WatchCategory(view);
		const int pending = g_pendingEntry.exchange(-1);
		if (pending >= 0 && pending < static_cast<int>(Config::entries.size())) {
			DispatchEntry(view, Config::entries[pending]);
			return;
		}
		if (g_selectTicks.load() > 0) {
			g_selectTicks.fetch_sub(1);
			if (TrySelectMod(view))
				g_selectTicks.store(0);
		}
	}

	class AdvanceHook
	{
	public:
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::JournalMenu::VTABLE[0] };
			_Advance = vtbl.write_vfunc(0x5, Advance);
		}

	private:
		static void Advance(RE::JournalMenu* a_this, float a_interval, std::uint32_t a_currentTime)
		{
			_Advance(a_this, a_interval, a_currentTime);
			Tick(a_this);
		}
		static inline REL::Relocation<decltype(Advance)> _Advance;
	};

	class JournalSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static JournalSink* GetSingleton()
		{
			static JournalSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
			RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event && std::string_view(a_event->menuName.c_str()) == RE::JournalMenu::MENU_NAME) {
				g_injected.store(false);
				g_pendingEntry.store(-1);
				g_selectTicks.store(0);
				g_selectTarget.clear();
				g_catWatchTicks.store(0);
				g_catLastSize.store(-1);
				g_injectTicks.store(a_event->opening ? 150 : 0);
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	void Register()
	{
		AdvanceHook::Install();
		if (auto ui = RE::UI::GetSingleton())
			ui->AddEventSink<RE::MenuOpenCloseEvent>(JournalSink::GetSingleton());
	}
}
static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message)
{
	switch (message->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		{
			Config::Load();
			SystemMenuButton::Register();
		}
		break;
	}
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = "STB_SystemPage";
	a_info->version = 1;

	if (a_skse->IsEditor()) {
		logger::critical("Loaded in editor, marking as incompatible"sv);
		return false;
	}

	return true;
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;

	v.PluginVersion(1);
	v.PluginName("STB_SystemPage");
	v.AuthorName("STB");
	v.UsesAddressLibrary(true);
	v.CompatibleVersions({ SKSE::RUNTIME_SSE_LATEST});
	v.HasNoStructUse(true);

	return v;
}();

void InitializeLog()
{
	auto path = logger::log_directory();
	if (!path) {
		stl::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::info);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%l] %v"s);
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	SKSE::Init(a_skse);

	auto messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", SKSEMessageHandler)) {
		return false;
	}

	return true;
}
