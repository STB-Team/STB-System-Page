#pragma once

// ============================================================================
// STB System Page — public contract for third-party SKSE plugins (action.type
// == "skse_message"). Copy this header into your project and listen for messages
// from the "STB_SystemPage" plugin via the SKSE messaging interface:
//
//   SKSE::GetMessagingInterface()->RegisterListener("STB_SystemPage", OnMessage);
//
//   void OnMessage(SKSE::MessagingInterface::Message* m) {
//       if (m->type == STB::SystemPage::kMessage_MenuAction) {
//           auto* a = static_cast<const STB::SystemPage::ActionMessage*>(m->data);
//           // a->id / a->messageId are valid ONLY during the call — copy them.
//       }
//   }
//
// The message is dispatched synchronously at the moment a menu entry is clicked
// (on the main thread, from the JournalMenu::AdvanceMovie phase).
// ============================================================================

#include <cstdint>

namespace STB::SystemPage
{
	// Sender plugin name (for RegisterListener).
	inline constexpr const char* kPluginName = "STB_SystemPage";

	enum : std::uint32_t
	{
		// Click on a menu entry with action.type == "skse_message".
		kMessage_MenuAction = 'STBa',
	};

	// Payload of kMessage_MenuAction. The pointers are only alive during the
	// listener call — copy the strings if you need them later.
	struct ActionMessage
	{
		const char* id;         // Config entry.id from JSON.
		const char* messageId;  // action.messageId from JSON.
	};
}
