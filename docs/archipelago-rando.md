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

- [x] **Present-demo gets** (`daDitem_c`, `src/d/actor/d_a_demo_item.cpp`) — chests and
      most story/NPC "hold-up" gifts. Gated at the `execItemGet` in `actionEvent()`.
- [ ] Freestanding field items (`d_a_obj_item`) — `execItemGet(m_itemNo)`; suppress only
      randomized instances.
- [ ] `d_a_demo00` special gets (e.g. case 3).
- [ ] Dungeon items: small keys, big key, map, compass.
- [ ] Heart pieces / containers.
- [ ] Poes, Golden Bugs, Sky characters, Hidden skills (golden wolves).
- [ ] Shop items.
- [ ] Boss rewards, fishing, minigame rewards, NPC quest items.

## Per-category procedure

1. Find the in-game give-site (use zsrtp/Randomizer's check list to enumerate sources).
2. Gate the vanilla item-give with `randoActive()`.
3. Confirm the check flag still sets (client detection) and there's no softlock.
4. Verify AP delivers the placed item (the held-up/visible item may still be the
   vanilla one — cosmetic; the received item is authoritative).

## Notes

- Fully-remote needs **no seed/placement data in-game** — AP delivers items and the
  world enforces logic at generation; only the `randoActive` signal is required.
- Environmental drops use separate actors (`d_a_obj_drop`, etc.) and are left intact.
- Our AP grants call `execItemGet` directly from the module, bypassing these gates.

## Risks

- The present demo visually shows the *vanilla* item (cosmetic mismatch).
- Story-forced items the apworld doesn't place would be lost — an apworld-completeness
  issue, not an engine one.
- Distinguishing randomized vs non-randomized freestanding items needs the check list.
