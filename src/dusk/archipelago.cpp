// Dusklight Archipelago bridge module.
//
// Native replacement for the GC randomizer REL + dolphin_memory_engine path:
//   1. tiny localhost TCP text server (Dusk = server, AP Python client = client),
//   2. read/write window over the live dSv_info_c (so the apworld client reads
//      location flags / health / name / stage at the same offsets, unchanged),
//   3. each frame drains the item queue the client writes into dSv reserve (off
//      0x8F0) and grants each item via the decomp's execItemGet() (the REL's job),
//   4. a native "safe to give?" gate.
//
// Socket boilerplate mirrors src/dusk/livesplit.cpp. Protocol + offset map: see
// Dusklight-AP/DESIGN.md.

#if _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
    static void closeSocket(socket_t s) { closesocket(s); }
    static bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    using socket_t = int;
    static const socket_t INVALID_SOCKET = -1;
    static void closeSocket(socket_t s) { close(s); }
    static bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
#endif

#include <cstdio>
#include <cstring>
#include <string>

#include "dusk/archipelago.h"
#include "d/d_save.h"             // dSv_info_c, dSv_memBit_c, dSv_event_flag_c
#include "d/d_com_inf_game.h"     // dComIfGs_getSaveInfo(), dComIfGp_getStageStagInfo()
#include "d/d_item.h"             // execItemGet()
#include "d/d_stage.h"            // dStage_SaveTbl, dStage_stagInfo_GetSaveTbl()

namespace dusk::archipelago {

namespace {

constexpr int kItemQueueOff = 0x8F0;  // dSv reserve: 8-slot item queue (client ITEM_WRITE_ADDR)
constexpr int kItemQueueLen = 8;
constexpr int kNodeOff      = 0x978;  // mDan.mStageNo (s8); 0xFF/-1 == not in a stage
constexpr int kNameOff      = 0x1B4;  // mPlayerInfo.mPlayerName
constexpr int kWindowMax    = static_cast<int>(sizeof(dSv_info_c));

socket_t    g_listen   = INVALID_SOCKET;
socket_t    g_client   = INVALID_SOCKET;
int         g_port     = 17354;
bool        g_initDone = false;
std::string g_rx;

inline u8* saveBase() { return reinterpret_cast<u8*>(dComIfGs_getSaveInfo()); }

inline bool inGame() {
    u8* b = saveBase();
    return b != nullptr && static_cast<s8>(b[kNodeOff]) != -1;
}

// Native equivalent of the client's _check_status(): the player actor exists and no
// event/demo/cutscene is running, so execItemGet() never fires mid-cutscene. Both
// accessors route through the always-constructed g_dComIfG_gameInfo.play, so they are
// safe to call even on the title/file-select screen (they return null / 0 there).
bool safeToGive() {
    return dComIfGp_getPlayer(0) != nullptr && !dComIfGp_event_runCheck();
}

// ---------------------------------------------------------------------------
// Per-dungeon item delivery
//
// The apworld's item_id is the zsrtp-randomizer id, which is NOT always a base-game
// dItemNo. Most ids coincide (rupees, ammo, equipment, bottles, bugs) and are
// granted fine by execItemGet(). But per-dungeon DUNGEON ITEMS use rando ids that
// the base game maps to item_func_noentry (does nothing) -- this is why only rupees
// arrived in early testing. They must instead be applied directly to the target
// dungeon's saved memory-bit block:
//   small keys 0x85-0x8D, big/boss keys 0x92-0x98,
//   compasses 0x99/0x9A/0x9B + 0xA8-0xAD, maps 0xB6-0xBE.
// Mapping derived from ap/worlds/twilight_princess_dusklight/Items.py.

// The 9 main dungeons in apworld order (Forest..Hyrule Castle) -> dStage save-table
// index. LV1..LV9 == 16..24.
constexpr int kDungeonSaveTbl[9] = {
    dStage_SaveTbl_LV1, dStage_SaveTbl_LV2, dStage_SaveTbl_LV3,
    dStage_SaveTbl_LV4, dStage_SaveTbl_LV5, dStage_SaveTbl_LV6,
    dStage_SaveTbl_LV7, dStage_SaveTbl_LV8, dStage_SaveTbl_LV9,
};

// Apply fn to a dungeon's memory-bit block. If the player is currently inside that
// dungeon we mutate the live working copy (which the game persists into mSave on
// stage exit); otherwise we mutate the persistent per-stage copy (which the game
// loads into the live copy when the player next enters). Either way a received
// key/map/compass/boss-key lands correctly wherever the player is when AP delivers.
template <typename F>
void withDungeonBit(int dungeonIdx, F&& fn) {
    if (dungeonIdx < 0 || dungeonIdx >= 9) return;
    dSv_info_c* info = dComIfGs_getSaveInfo();
    if (!info) return;
    int saveTbl = kDungeonSaveTbl[dungeonIdx];
    stage_stag_info_class* si = dComIfGp_getStageStagInfo();
    if (si && dStage_stagInfo_GetSaveTbl(si) == saveTbl) {
        fn(info->getMemory().getBit());                     // live (in this dungeon)
    } else {
        fn(info->getSavedata().getSave(saveTbl).getBit());  // persistent
    }
}

// Returns true if id was a per-dungeon item we applied directly (so the caller must
// NOT also run execItemGet, which would hit item_func_noentry).
bool grantDungeonItem(u8 id) {
    // Small keys: 0x85..0x8D == Forest..Hyrule Castle; each grant adds one key.
    if (id >= 0x85 && id <= 0x8D) {
        withDungeonBit(id - 0x85, [](dSv_memBit_c& b) { b.setKeyNum(b.getKeyNum() + 1); });
        return true;
    }
    // Maps: 0xB6..0xBE == Forest..Hyrule Castle.
    if (id >= 0xB6 && id <= 0xBE) {
        withDungeonBit(id - 0xB6, [](dSv_memBit_c& b) { b.onDungeonItemMap(); });
        return true;
    }
    // Big/boss keys (only the 7 dungeons that use a standard boss key; Goron Mines
    // uses key shards and Snowpeak the Bedroom Key, both real item_funcs).
    switch (id) {
        case 0x92: withDungeonBit(0, [](dSv_memBit_c& b) { b.onDungeonItemBossKey(); }); return true; // Forest
        case 0x93: withDungeonBit(2, [](dSv_memBit_c& b) { b.onDungeonItemBossKey(); }); return true; // Lakebed
        case 0x94: withDungeonBit(3, [](dSv_memBit_c& b) { b.onDungeonItemBossKey(); }); return true; // Arbiters
        case 0x95: withDungeonBit(5, [](dSv_memBit_c& b) { b.onDungeonItemBossKey(); }); return true; // Temple of Time
        case 0x96: withDungeonBit(6, [](dSv_memBit_c& b) { b.onDungeonItemBossKey(); }); return true; // City in the Sky
        case 0x97: withDungeonBit(7, [](dSv_memBit_c& b) { b.onDungeonItemBossKey(); }); return true; // Palace of Twilight
        case 0x98: withDungeonBit(8, [](dSv_memBit_c& b) { b.onDungeonItemBossKey(); }); return true; // Hyrule Castle
        default: break;
    }
    // Compasses: 0x99/0x9A/0x9B then 0xA8..0xAD == Forest..Hyrule Castle.
    switch (id) {
        case 0x99: withDungeonBit(0, [](dSv_memBit_c& b) { b.onDungeonItemCompass(); }); return true;
        case 0x9A: withDungeonBit(1, [](dSv_memBit_c& b) { b.onDungeonItemCompass(); }); return true;
        case 0x9B: withDungeonBit(2, [](dSv_memBit_c& b) { b.onDungeonItemCompass(); }); return true;
        case 0xA8: withDungeonBit(3, [](dSv_memBit_c& b) { b.onDungeonItemCompass(); }); return true;
        case 0xA9: withDungeonBit(4, [](dSv_memBit_c& b) { b.onDungeonItemCompass(); }); return true;
        case 0xAA: withDungeonBit(5, [](dSv_memBit_c& b) { b.onDungeonItemCompass(); }); return true;
        case 0xAB: withDungeonBit(6, [](dSv_memBit_c& b) { b.onDungeonItemCompass(); }); return true;
        case 0xAC: withDungeonBit(7, [](dSv_memBit_c& b) { b.onDungeonItemCompass(); }); return true;
        case 0xAD: withDungeonBit(8, [](dSv_memBit_c& b) { b.onDungeonItemCompass(); }); return true;
        default: break;
    }
    return false;
}

// Hidden-skill event flags in apworld progressive order (same set ImGuiSaveEditor
// uses). Each "Progressive Hidden Skill" grant learns the next unlearned skill.
const u16 kHiddenSkillFlags[7] = {
    dSv_event_flag_c::F_0339, dSv_event_flag_c::F_0338, dSv_event_flag_c::F_0340,
    dSv_event_flag_c::F_0341, dSv_event_flag_c::F_0342, dSv_event_flag_c::F_0343,
    dSv_event_flag_c::F_0344,
};

// Progressive items: the apworld sends ONE id per copy, so a flat execItemGet would
// grant the same tier every time (and several base funcs are stubs / noentry).
// Escalate based on current save state instead. Returns true if handled.
bool grantProgressive(u8 id) {
    switch (id) {
        case 0x44:  // Progressive Clawshot (2): Clawshot -> Double Clawshots
            if (!dComIfGs_isItemFirstBit(0x44)) execItemGet(0x44);  // single clawshot
            else execItemGet(0x47);                                  // double (W_HOOKSHOT)
            return true;
        case 0x36: {  // Progressive Wallet (2): normal -> big -> giant
            u8 w = dComIfGs_getWalletSize();
            if (w < GIANT_WALLET) dComIfGs_setWalletSize(static_cast<u8>(w + 1));
            return true;
        }
        case 0xA5:  // Progressive Mirror Shard (4): MIRROR_PIECE_* funcs are stubs, so
                    // set the pieces directly. getMirrorNum() counts bits 0..3
                    // consecutively, so fill them in order.
            for (u8 i = 0; i < 4; ++i) {
                if (!dComIfGs_isCollectMirror(i)) { dComIfGs_onCollectMirror(i); break; }
            }
            return true;
        case 0xD8:  // Progressive Fused Shadow (3) -> collect next crystal
            for (u8 i = 0; i < 3; ++i) {
                if (!dComIfGs_isCollectCrystal(i)) { dComIfGs_onCollectCrystal(i); break; }
            }
            return true;
        case 0xE1:  // Progressive Hidden Skill (7) -> learn next skill
            for (u16 f : kHiddenSkillFlags) {
                if (!dComIfGs_isEventBit(f)) { dComIfGs_onEventBit(f); break; }
            }
            return true;
        case 0xE9: {  // Progressive Sky Book (7): Ancient Sky Book + 6 sky characters.
            // Tier 1 = the book; tiers 2..7 set the sky-character EVENT bits (the same
            // ones the owl-statue cutscene sets, see d_a_tag_statue_evt l_event_bit).
            // These are distinct from the "Owl Statue Sky Character" LOCATION flags
            // (those are region/area flags), so this won't false-trigger checks. The
            // 6th character completes the book (F_0796 + the filled-book item).
            if (!dComIfGs_isItemFirstBit(0xE9)) { execItemGet(0xE9); return true; }  // book
            static const u16 kSkyChars[6] = {
                dSv_event_flag_c::F_0791, dSv_event_flag_c::F_0792, dSv_event_flag_c::F_0793,
                dSv_event_flag_c::F_0794, dSv_event_flag_c::F_0795, dSv_event_flag_c::F_0812,
            };
            int n = 0;
            for (u16 f : kSkyChars) if (dComIfGs_isEventBit(f)) ++n;
            if (n < 6) {
                dComIfGs_onEventBit(kSkyChars[n]);
                if (n == 5) {                              // 6th character -> book complete
                    dComIfGs_onEventBit(dSv_event_flag_c::F_0796);
                    execItemGet(0xEB);                     // ANCIENT_DOCUMENT2 (filled book)
                } else {
                    execItemGet(0xEA);                     // AIR_LETTER (partial book)
                }
            }
            return true;
        }
        default:
            return false;
    }
}

// Grant a TP item by id (see Items.py ITEM_TABLE):
//  - per-dungeon dungeon items   -> applied to the target dungeon's save block;
//  - fused shadows / hidden skills -> applied as progressives;
//  - everything else             -> execItemGet() (the decomp's normal dispatch).
// The id is logged so the queue drain is self-diagnosing in the console.
//
// TODO(rando): remaining progressives still pass a fixed id to execItemGet. These are
// "playable as-is" because tier 1 already grants a working item, they just don't reach
// higher tiers: Master Sword 0x29 (-> always Master, skips Ordon; fine), Bow 0x43
// (quiver never grows past 30), Dominion Rod 0x46 (gives charged rod; fine), Fishing
// Rod 0x4A, Bomb Bag 0x51 (extra bomb-type bags). Stubs to verify: Hylian Shield 0x2C
// / Ordon Shield 0x2B (empty funcs), Giant Bomb Bag 0x4F (empty), Shadow Crystal 0x32
// (-> MAGIC_LV1). Sky Book 0xE9 is handled in grantProgressive but the full Shad ->
// Sky Cannon -> City in the Sky flow is untested at endgame.
void grantItem(u8 itemId) {
    if (itemId == 0x00) return;
    std::printf("[AP] grant id=0x%02X\n", itemId);
    if (grantDungeonItem(itemId)) return;
    if (grantProgressive(itemId)) return;
    execItemGet(itemId);
}

bool windowRead(int off, int len, std::string& outHex) {
    if (off < 0 || len < 0 || off + len > kWindowMax) return false;
    u8* base = saveBase();
    if (!base) return false;
    static const char* hex = "0123456789abcdef";
    outHex.clear();
    outHex.reserve(static_cast<size_t>(len) * 2);
    for (int i = 0; i < len; ++i) {
        u8 b = base[off + i];
        outHex.push_back(hex[b >> 4]);
        outHex.push_back(hex[b & 0xF]);
    }
    return true;
}

int nib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool windowWrite(int off, const char* hex) {
    int len = static_cast<int>(std::strlen(hex) / 2);
    if (off < 0 || off + len > kWindowMax) return false;
    u8* base = saveBase();
    if (!base) return false;
    for (int i = 0; i < len; ++i) {
        int hi = nib(hex[i * 2]), lo = nib(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        base[off + i] = static_cast<u8>((hi << 4) | lo);
    }
    return true;
}

void sendLine(const std::string& s) {
    if (g_client == INVALID_SOCKET) return;
    std::string out = s;
    out.push_back('\n');
    // Reliable send: the socket is non-blocking and responses can be several KB
    // (the client snapshots the whole dSv_info_c window in one READ), so a single
    // send() may not flush everything. Loop on partial sends; on a full send buffer
    // (wouldBlock) spin briefly, but bail after a bounded number of retries so a
    // stalled client can never freeze the game's frame thread.
    size_t sent = 0;
    int spins = 0;
    while (sent < out.size()) {
        int n = send(g_client, out.data() + sent, static_cast<int>(out.size() - sent), 0);
        if (n > 0) { sent += static_cast<size_t>(n); spins = 0; continue; }
        if (n < 0 && wouldBlock() && ++spins < 100000) continue;  // buffer full, retry
        closeSocket(g_client);                                    // hard error / stuck
        g_client = INVALID_SOCKET;
        return;
    }
}

void handleCommand(const std::string& line) {
    if (line.rfind("HELLO", 0) == 0) {
        std::string nameHex;
        windowRead(kNameOff, 16, nameHex);
        char buf[48];
        std::snprintf(buf, sizeof(buf), "OK E %d ", inGame() ? 1 : 0);  // region E = GZ2E01 (US)
        sendLine(std::string(buf) + nameHex);
        return;
    }
    if (line.rfind("READ ", 0) == 0) {
        int off = 0, len = 0;
        std::string hex;
        if (std::sscanf(line.c_str() + 5, "%d %d", &off, &len) == 2 && windowRead(off, len, hex)) {
            sendLine("OK " + hex);
            return;
        }
        sendLine("ERR");
        return;
    }
    if (line.rfind("WRITE ", 0) == 0) {
        int off = 0;
        char hexbuf[2048] = {0};
        if (std::sscanf(line.c_str() + 6, "%d %2047s", &off, hexbuf) == 2 && windowWrite(off, hexbuf)) {
            sendLine("OK");
            return;
        }
        sendLine("ERR");
        return;
    }
    if (line.rfind("SAFE", 0) == 0) {
        sendLine(std::string("OK ") + (safeToGive() ? "1" : "0"));
        return;
    }
    sendLine("ERR");
}

void setNonBlocking(socket_t s) {
#if _WIN32
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
}

}  // namespace

void init(int port) {
    g_initDone = true;
    g_port = port;
#if _WIN32
    WSADATA wd{};
    WSAStartup(MAKEWORD(2, 2), &wd);
#endif
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen == INVALID_SOCKET) return;
    int yes = 1;
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(g_port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (bind(g_listen, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(g_listen, 1) != 0) {
        closeSocket(g_listen);
        g_listen = INVALID_SOCKET;
        return;
    }
    setNonBlocking(g_listen);
    std::printf("[AP] listening on 127.0.0.1:%d\n", g_port);
}

void onGameFrame() {
    if (!inGame() || !safeToGive()) return;
    u8* base = saveBase();
    if (!base) return;
    for (int i = 0; i < kItemQueueLen; ++i) {
        u8 id = base[kItemQueueOff + i];
        if (id != 0x00) {
            grantItem(id);
            base[kItemQueueOff + i] = 0x00;  // ack: clear slot (mirrors the REL)
        }
    }
}

void update() {
    if (!g_initDone) init(g_port);  // lazy-init the listener on first frame
    if (g_listen == INVALID_SOCKET) return;

    if (g_client == INVALID_SOCKET) {
        socket_t c = accept(g_listen, nullptr, nullptr);
        if (c != INVALID_SOCKET) {
            setNonBlocking(c);
            int one = 1;  // disable Nagle: bridge traffic is small request/response
            setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
            g_client = c;
            g_rx.clear();
            std::printf("[AP] client connected\n");
        }
        return;
    }

    char buf[2048];
    int n = recv(g_client, buf, sizeof(buf), 0);
    if (n == 0 || (n < 0 && !wouldBlock())) {
        closeSocket(g_client);
        g_client = INVALID_SOCKET;
        std::printf("[AP] client disconnected\n");
        return;
    }
    if (n > 0) {
        g_rx.append(buf, n);
        size_t nl;
        while ((nl = g_rx.find('\n')) != std::string::npos) {
            std::string ln = g_rx.substr(0, nl);
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            g_rx.erase(0, nl + 1);
            if (!ln.empty()) handleCommand(ln);
        }
    }
}

void shutdown() {
    if (g_client != INVALID_SOCKET) { closeSocket(g_client); g_client = INVALID_SOCKET; }
    if (g_listen != INVALID_SOCKET) { closeSocket(g_listen); g_listen = INVALID_SOCKET; }
#if _WIN32
    WSACleanup();
#endif
}

bool isListening()       { return g_listen != INVALID_SOCKET; }
bool isClientConnected() { return g_client != INVALID_SOCKET; }
int  listenPort()        { return g_port; }
bool randoActive()       { return g_client != INVALID_SOCKET; }

}  // namespace dusk::archipelago
