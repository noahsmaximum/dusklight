#pragma once
// Dusklight Archipelago bridge module.
//
// Lifecycle (wired next to dusk::speedrun):
//   dusk::archipelago::onGameFrame();  // per game frame: drain item queue, grant items
//   dusk::archipelago::update();       // per frame: service socket (lazy-inits the listener)
//   dusk::archipelago::shutdown();     // optional, at exit
// See Dusklight-AP/DESIGN.md for the bridge protocol and the dSv_info_c offset map.

#include <cstdint>

namespace dusk::archipelago {

void init(int port = 17354);
void onGameFrame();
void update();
void shutdown();

bool isListening();
bool isClientConnected();
int  listenPort();

// True when a randomizer client is connected, i.e. the game should run in
// fully-remote mode: locations register their check but suppress their vanilla
// item-give, and all items are delivered by the client via execItemGet.
bool randoActive();

// Display overrides: the client scouts the seed and pushes a placement table
// (PLCS command) mapping location keys -> the item id to SHOW at that location
// (the AP-placed item for own-world, a placeholder for other players' items).
// These return the display id for a give-site, or vanillaId when no override
// applies. The caller is responsible for verifying the returned id has model
// resources for its path (demo arc vs field arc) before using it.
uint8_t displayForTbox(int bitNo, uint8_t vanillaId);      // chest, keyed by tbox flag no
uint8_t displayForItemFlag(int bitNo, uint8_t vanillaId);  // freestanding, keyed by item flag no

// Item id repurposed as the off-world placeholder (the client sends it for other
// players' items). 0xAE = dItemNo_NOENTRY_174_e, an unused slot whose item-resource
// row is retargeted at the Sol model (d_item_data.cpp). NOTE: this is NOT
// dItemNo_LIGHT_DROP_e (0xA0).
constexpr uint8_t kPlaceholderItemNo = 0xAE;

}  // namespace dusk::archipelago
