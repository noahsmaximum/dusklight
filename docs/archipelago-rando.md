# Archipelago randomizer engine — native port (design & roadmap)

Goal: a logic-correct Archipelago randomizer on Dusklight, using the **fully-remote**
model: each randomized location registers its check (the game's normal save-flag set),
but its **vanilla item is suppressed**, and the **AP client delivers every item** via
the native bridge (`dusk::archipelago` → `execItemGet`).

Reference: [zsrtp/Randomizer](https://github.com/zsrtp/Randomizer) — the official TP
randomizer REL that the Dolphin-based AP setup relies on. We adapt its per-location
logic to native source edits rather than runtime REL patching.

## Gate

`dusk::archipelago::randoActive()` — true when an AP client is connected. All
suppression is gated on it, so normal (non-AP) play is unchanged.

## Why this can't be one global hook

Randomized locations **and** environmental drops (grass/pot rupees & hearts, ammo)
both call `execItemGet`, and chest item-gives are routed through Link's *present demo*.
So suppression must be applied **per source/category**, not globally.

## Categories & status

- [x] **Present-demo gets** (`daDitem_c::actionEvent`, `d_a_demo_item.cpp`). This actor
      is created by BOTH `fopAcM_createItemForTrBoxDemo` and
      `fopAcM_createItemForPresentDemo`, so gating its `execItemGet` covers a huge set:
      **chests, small keys, heart pieces/containers, poe souls, sky-character book
      upgrades, in-chest map/compass/boss keys, boss heart containers, and story/NPC
      hold-up gifts.** Each source sets its own check flag (`onTbox`/`onItem`/event)
      separately, so the check still registers.
- [x] Freestanding field items (`d_a_obj_item::itemGet`) — gated when the instance is
      tracked (`mItemBitNo != 0xFF`); environmental/respawning drops (0xFF) are left
      intact. The pickup flag (`fopAcM_onItem`) is set by `itemGetNextExecute`, so the
      check still registers.
- [x] Fishing-hole heart piece (`d_a_mg_rod`) — direct `execItemGet(KAKERA_HEART)` gated;
      `onItem` flag + fishing event reg kept.
- [x] **Master Sword pedestal** — both give-sites gated (`d_a_demo00` case 3 and
      `d_a_obj_master_sword`, which set the sword collect/equip directly). VERIFIED safe:
      the "Sacred Grove Pedestal Master Sword"/"Pedestal Shadow Crystal" checks are one
      shared event flag (save `0x811` bit `0x20`) set by the demo via
      `onEventBit(getFlagNo())`, which is kept. The pedestal heal is kept too.
- [x] **Light Sword** (`d_a_obj_swBallC` case 10, Palace of Twilight Sols) — gated;
      "Collect Both Sols" is a region-flag check, unaffected.
- [x] **Hidden skills** — the lesson sets `F_0338..F_0344` via the generic message-flow
      "Event Flag ON" node (`dMsgFlow_c::event000`, label indices 338–344). Gated exactly
      those indices when rando is active; the golden-wolf location-check flags are
      different indices (e.g. `0x3D80`) and still set. AP's Progressive Hidden Skill is
      authoritative.
- [x] Golden bugs, shops, minigame rewards, Agitha, NPC quest gifts — audited: **no
      direct `execItemGet` sites**; they all route through the present-demo actor, which
      is already gated (category 1).

## Per-category procedure

1. Find the in-game give-site (use zsrtp/Randomizer's check list to enumerate sources).
2. Gate the vanilla item-give with `randoActive()`.
3. Confirm the check flag still sets (client detection) and there's no softlock.
4. Verify AP delivers the placed item (the held-up/visible item may still be the
   vanilla one — cosmetic; the received item is authoritative).

## Item delivery (grant side)

Suppression (above) is one axis; **delivering** the AP-placed item is the other.
The client writes the apworld `item_id` into the native item queue and the module's
`grantItem()` applies it. The apworld id is the **zsrtp-randomizer id**, not always a
base-game `dItemNo`:

- Most ids coincide and are granted by `execItemGet()` (rupees, ammo, equipment,
  bottles, bugs, poe souls, scents…). This is why rupees worked first.
- **Per-dungeon dungeon items** use ids the base game maps to `item_func_noentry`
  (silently nothing — the original "only rupees arrive" bug). `grantItem()` now
  applies these directly to the target dungeon's saved `dSv_memBit_c`
  (`getSave(dStage_SaveTbl_LVn)` persistent, or live `getMemory()` if the player is
  in that dungeon): small keys `0x85-0x8D`, big/boss keys `0x92-0x98`, compasses
  `0x99/0x9A/0x9B + 0xA8-0xAD`, maps `0xB6-0xBE`.
- **Progressives** handled in `grantProgressive()` (escalate by save state): Clawshot
  `0x44` (single → Double Clawshots `0x47`), Wallet `0x36` (normal→big→giant via
  `setWalletSize`), Mirror Shard `0xA5` (`onCollectMirror` bits 0..3 in order —
  `getMirrorNum()` counts them consecutively; the `MIRROR_PIECE_*` funcs are stubs),
  Fused Shadow `0xD8` (`onCollectCrystal`), Hidden Skill `0xE1` (event flags), Sky Book
  `0xE9` (the book, then the 6 sky-character event bits `F_0791`..`F_0795`/`F_0812`; the
  6th sets `F_0796` + the filled-book item — these event bits are distinct from the
  `Region`-type "Owl Statue Sky Character" location flags, so no false checks).

### Delivery TODO — remaining progressives / stubs

- Sky Book `0xE9` is handled, but the full in-game Shad → Sky Cannon → City in the Sky
  flow is untested at endgame (verify when someone reaches it).
- Playable as-is but don't reach higher tiers: Master Sword `0x29` (always Master,
  skips Ordon), Bow `0x43` (quiver stays 30), Dominion Rod `0x46` (gives charged rod),
  Fishing Rod `0x4A`, Bomb Bag `0x51` (extra bomb-type bags).
- Empty `item_func` stubs to verify: Hylian Shield `0x2C` / Ordon Shield `0x2B`, Giant
  Bomb Bag `0x4F`; Shadow Crystal `0x32` → `item_func_MAGIC_LV1`.
- Oddball keys still on `execItemGet`/`noentry`: Bulblin Camp `0x8E`, Gate Keys
  `0xF3`, Goron Key Shards `0xF9`, Ordon Pumpkin/Cheese `0xF4`/`0xF5`.

`grantItem()` logs every drained id (`[AP] grant id=0x..`) so the queue drain is
self-diagnosing in the console.

## Notes

- Fully-remote needs **no seed/placement data in-game** — AP delivers items and the
  world enforces logic at generation; only the `randoActive` signal is required.
- Environmental drops use separate actors (`d_a_obj_drop`, etc.) and are left intact.
- Item delivery is **entirely native** — the apworld already sends correct ids, so no
  client change was needed to fix dungeon-item delivery.

## Risks

- The present demo visually shows the *vanilla* item (cosmetic mismatch).
- Story-forced items the apworld doesn't place would be lost — an apworld-completeness
  issue, not an engine one.
- Distinguishing randomized vs non-randomized freestanding items needs the check list.
