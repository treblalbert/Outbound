#include "audio.h"
#include "game.h"
#include "input.h"
#include "lang.h"
#include "prompt.h"
#include "sprites.h"
#include "ui.h"

void raid_dropItem(const Item& it);

static std::vector<Item>* g_other = nullptr;
static int g_otherSlots = 0;
static std::vector<Item>* g_dragSlots = nullptr;
static int g_dragIndex = -1;
static bool g_dragCountsMission = false;
// A controller has no button to hold while it drags (0.12v): Y picks the item up, and A
// or Y on another slot puts it down there. The item is carried until then.
static bool g_padDrag = false;
static unsigned g_padDragFrame = 0;      // last UI frame an inventory was drawn in
static void endDrag() { g_dragSlots = nullptr; g_dragIndex = -1; g_dragCountsMission = false; g_padDrag = false; }

// Two worn vests of the same kind, one dropped on the other: the plates are pooled
// into the one it lands on, up to a full vest; anything past that stays in the other.
// True when they were merged.
static bool mergeArmor(Item& from, Item& to) {
    if (from.empty() || to.empty() || from.id != to.id || itemDef(from.id).cat != Cat::Armor) return false;
    int full = (int)itemDef(to.id).param;
    if (to.data >= full) { setNotice(T("That vest is already in full condition.")); return true; }
    int total = to.data + from.data;
    to.data = std::min(full, total);
    int left = total - to.data;
    if (left > 0) from.data = left;
    else from = Item();
    int pct = (int)std::round(100.0f * to.data / std::max(1, full));
    setNotice(left > 0 ? T1("Vests combined: one is now {0}%, the other keeps the rest.", std::to_string(pct))
                       : T1("Vests combined into one at {0}%.", std::to_string(pct)));
    Audio::play(Snd::pickup, 0.6f, 0.8f);
    return true;
}

static void dropDragged(std::vector<Item>& dst, int index) {
    if (!g_dragSlots || g_dragIndex < 0 || g_dragIndex >= (int)g_dragSlots->size() || index < 0 || index >= (int)dst.size()) return;
    Item& from = (*g_dragSlots)[g_dragIndex];
    Item& to = dst[index];
    if (&dst == g_dragSlots && index == g_dragIndex) return;
    int movedId = from.id, before = from.count;
    if (mergeArmor(from, to)) return;
    if (to.empty()) { to = from; from = Item(); }
    else if (to.id == from.id && itemDef(to.id).stack > 1) {
        int n = std::min<int>(from.count, itemDef(to.id).stack - to.count);
        to.count += n; from.count -= n; if (from.count <= 0) from = Item();
    } else std::swap(from, to);
    if (g_dragCountsMission && &dst != g_dragSlots && (from.id != movedId || from.count < before)) missionAddLoot(movedId);
    Audio::play(Snd::pickup, 0.45f, 1.1f);
}

void compactInventory() {
    auto& inv = G.prof.inv;
    std::vector<Item> out;
    for (auto& it : inv) if (!it.empty()) out.push_back(it);
    out.resize(INV_MAX_SLOTS);
    inv = out;
}

// Display order for the sort button: things you fight with first, junk last.
static int catOrder(Cat c) {
    switch (c) {
    case Cat::Weapon: case Cat::Melee: return 0;
    case Cat::Ammo: return 1;
    case Cat::Medical: return 2;
    case Cat::Throwable: return 3;
    case Cat::Armor: return 4;
    case Cat::Backpack: return 5;
    case Cat::Valuable: return 6;
    default: return 7;
    }
}

// Groups the inventory by kind, puts the valuable things first inside each group
// and merges any split stacks back together.
void sortInventory() {
    Profile& p = G.prof;
    int cap = std::min(p.invCapacity(), (int)p.inv.size());
    std::vector<Item> items;
    for (int i = 0; i < cap; i++)
        if (!p.inv[i].empty()) items.push_back(p.inv[i]);
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        const ItemDef& da = itemDef(a.id);
        const ItemDef& db = itemDef(b.id);
        int ca = catOrder(da.cat), cb = catOrder(db.cat);
        if (ca != cb) return ca < cb;
        if (da.rarity != db.rarity) return da.rarity > db.rarity;
        if (da.value != db.value) return da.value > db.value;
        return a.id < b.id;
    });
    for (int i = 0; i < cap; i++) p.inv[i] = Item();
    // Re-inserting merges equal ids, which are now adjacent.
    for (const Item& it : items) addToSlots(p.inv, it, cap);
}

static int usedInvSlots() {
    int n = 0;
    for (auto& it : G.prof.inv) if (!it.empty()) n++;
    return n;
}

// The equipment slots by number: 0-1 the guns, 2 armour, 3 backpack, 4 melee (0.12v).
static Item* equipSlot(int slot) {
    Profile& p = G.prof;
    return slot < 2 ? &p.weapons[slot] : slot == 2 ? &p.armor : slot == 3 ? &p.backpack : &p.melee;
}

static bool dropDraggedOnEquipment(int slot) {
    if (!g_dragSlots || g_dragIndex < 0 || g_dragIndex >= (int)g_dragSlots->size()) return false;
    Item& from = (*g_dragSlots)[g_dragIndex];
    if (from.empty()) return false;
    const Cat cat = itemDef(from.id).cat;
    if ((slot < 2 && cat != Cat::Weapon) || (slot == 2 && cat != Cat::Armor) ||
        (slot == 3 && cat != Cat::Backpack) || (slot == 4 && cat != Cat::Melee)) return false;

    Profile& p = G.prof;
    Item* target = equipSlot(slot);
    if (slot == 2 && mergeArmor(from, *target)) return true;   // onto the vest you wear
    auto invBackup = p.inv;
    auto srcBackup = *g_dragSlots;
    Item equipBackup = *target;
    int movedId = from.id;
    std::swap(from, *target);
    if (slot == 3) {
        compactInventory();
        if (usedInvSlots() > p.invCapacity()) {
            p.inv = invBackup;
            *g_dragSlots = srcBackup;
            *target = equipBackup;
            setNotice(T("Not enough space with that backpack."));
            return false;
        }
    }
    if (slot < 2) { p.curWeapon = slot; G.player.reloadT = 0; }
    if (g_dragCountsMission) missionAddLoot(movedId);
    Audio::play(Snd::pickup, 0.55f, 0.85f);
    return true;
}

bool equipFrom(std::vector<Item>& src, int index) {
    Profile& p = G.prof;
    if (index < 0 || index >= (int)src.size() || src[index].empty()) return false;
    const ItemDef& d = itemDef(src[index].id);
    switch (d.cat) {
    case Cat::Weapon: {
        int slot = p.curWeapon;
        if (!p.weapons[slot].empty() && p.weapons[1 - slot].empty()) slot = 1 - slot;
        std::swap(src[index], p.weapons[slot]);
        p.curWeapon = slot;
        G.player.reloadT = 0;
        Audio::play(Snd::reload, 0.5f, 1.3f);
        return true;
    }
    case Cat::Armor:
        std::swap(src[index], p.armor);
        Audio::play(Snd::pickup, 0.5f, 0.7f);
        return true;
    case Cat::Backpack: {
        auto invBackup = p.inv;
        auto srcBackup = src;
        Item packBackup = p.backpack;
        bool srcIsInv = &src == &p.inv;
        std::swap(src[index], p.backpack);
        if (!srcIsInv && !src[index].empty()) {
            // Old backpack goes back where the new one came from; nothing to do.
        }
        compactInventory();
        if (usedInvSlots() > p.invCapacity()) {
            p.inv = invBackup;
            if (!srcIsInv) src = srcBackup;
            p.backpack = packBackup;
            setNotice(T("Not enough space with that backpack."));
            pushMessage(T("Not enough space with that backpack"), P_CORAL);
            return false;
        }
        Audio::play(Snd::pickup, 0.5f, 0.7f);
        return true;
    }
    case Cat::Melee:
        std::swap(src[index], p.melee);
        Audio::play(Snd::pickup, 0.5f, 0.6f);
        return true;
    case Cat::Medical:
        return useItemAt(src, index);
    default:
        return false;
    }
}

bool useItemAt(std::vector<Item>& src, int index) {
    Profile& p = G.prof;
    if (index < 0 || index >= (int)src.size() || src[index].empty()) return false;
    const ItemDef& d = itemDef(src[index].id);
    if (d.cat == Cat::Medical) {
        bool bleeding = G.player.bleedT > 0;
        if (p.hp >= p.maxHp() - 0.5f && !bleeding) {
            pushMessage(T("Already at full health"), P_BEIGE);
            setNotice(T("Already at full health."));
            return false;
        }
        if (G.player.healCd > 0) return false;
        p.hp = std::min(p.maxHp(), p.hp + d.param);
        G.player.healCd = 0.8f;
        if (bleeding) {
            // Any dressing closes the wound, and holds for a while.
            G.player.bleedT = 0;
            G.player.bleedImmuneT = 20;
            pushMessage(T("Bleeding stopped."), P_LGREEN);
        }
        src[index].count--;
        if (src[index].count <= 0) src[index] = Item();
        Audio::play(Snd::heal, 0.7f);
        pushMessage(T1("Used {0}", T(d.name)), P_LGREEN);
        return true;
    }
    if (d.cat == Cat::Weapon || d.cat == Cat::Armor || d.cat == Cat::Backpack || d.cat == Cat::Melee) return equipFrom(src, index);
    return false;
}

bool unequip(int slot, std::vector<Item>* dest, int destSlots) {
    Profile& p = G.prof;
    Item* e = equipSlot(slot);
    if (e->empty()) return false;
    if (slot == 3) {
        compactInventory();
        int newCap = p.invCapacity() - itemDef(e->id).param;
        int need = usedInvSlots() + (dest ? 0 : 1);
        if (need > newCap) {
            pushMessage(T("Empty the backpack first"), P_CORAL);
            setNotice(T("Empty the backpack first."));
            return false;
        }
        std::vector<Item>& target = dest ? *dest : p.inv;
        int ts = dest ? destSlots : newCap;
        if (addToSlots(target, *e, ts) > 0) { setNotice(T("No space.")); return false; }
        *e = Item();
        return true;
    }
    std::vector<Item>& target = dest ? *dest : p.inv;
    int ts = dest ? destSlots : p.invCapacity();
    Item copy = *e;
    if (addToSlots(target, copy, ts) > 0) {
        pushMessage(T("No space"), P_CORAL);
        setNotice(T("No space."));
        return false;
    }
    *e = Item();
    if (slot < 2) G.player.reloadT = 0;
    return true;
}

int moveItem(std::vector<Item>& src, int index, std::vector<Item>& dst, int dstSlots) {
    if (src[index].empty()) return 0;
    int before = src[index].count;
    int left = addToSlots(dst, src[index], dstSlots);
    if (left <= 0) src[index] = Item();
    else src[index].count = (int16_t)left;
    return before - left;
}

void drawSlotGrid(float x, float y, std::vector<Item>& slots, int count, int cols, InvMode mode, bool isOther) {
    Profile& p = G.prof;
    // A controller's carried item is dropped if its panel went away for a frame.
    if (g_padDrag && UI::frameNo() > g_padDragFrame + 1) endDrag();
    if (g_padDrag) g_padDragFrame = UI::frameNo();
    if (g_dragSlots && !g_padDrag && !Input::mouseHeld(0) && !Input::mouseReleased(0)) endDrag();
    count = std::min(count, (int)slots.size());
    for (int i = 0; i < count; i++) {
        float sx = x + (i % cols) * SLOT, sy = y + (i / cols) * SLOT;
        int click = UI::itemSlot(sx, sy, slots[i]);
        bool hov = UI::hover(sx, sy, SLOT, SLOT);
        if (hov && Input::padGrabPressed()) {
            if (!g_dragSlots && !slots[i].empty()) {
                g_dragSlots = &slots; g_dragIndex = i;
                g_dragCountsMission = isOther && mode == InvMode::Loot;
                g_padDrag = true;
                g_padDragFrame = UI::frameNo();
                Audio::play(Snd::click, 0.4f, 1.3f);
                click = 0;
                continue;
            }
            if (g_padDrag) click = 1;   // Y again: put it down here
        }
        if (g_padDrag && click == 1 && hov) {
            if (!(g_dragSlots == &slots && g_dragIndex == i)) dropDragged(slots, i);
            endDrag();
            continue;
        }
        if (click == 1 && !slots[i].empty() && Input::mouseHeld(0)) {
            g_dragSlots = &slots; g_dragIndex = i;
            g_dragCountsMission = isOther && mode == InvMode::Loot;
            click = 0; // a release on the same slot remains a normal click
        }
        if (g_dragSlots && Input::mouseReleased(0) && UI::hover(sx, sy, SLOT, SLOT)) {
            bool same = g_dragSlots == &slots && g_dragIndex == i;
            if (same) click = 1;
            else dropDragged(slots, i);
            g_dragSlots = nullptr; g_dragIndex = -1; g_dragCountsMission = false;
        }
        if (mode == InvMode::Trader && !isOther && !slots[i].empty() && UI::hover(sx, sy, SLOT, SLOT))
            UI::itemTooltip(slots[i], T1("Left-click: SELL for ${0}", std::to_string(itemValue(slots[i]))));
        if (!click || slots[i].empty()) continue;
        
        // Usage bar for health items (medkits, bandages)
        if (slots[i].id == IT_MEDKIT || slots[i].id == IT_BANDAGE) {
            const ItemDef& d = itemDef(slots[i].id);
            // Show how much healing would be used
            float needed = p.maxHp() - p.hp;
            float healAmt = (slots[i].id == IT_MEDKIT) ? d.param : d.param * 0.5f;
            float barFrac = std::min(1.0f, needed / healAmt);
            UI::bar(sx + 2, sy + SLOT - 4, SLOT - 4, 2, barFrac, P_LGREEN);
        }
        // Usage bar for ammo (show magazine vs reserve)
        if (itemDef(slots[i].id).cat == Cat::Ammo) {
            // Check if this ammo type is used by current weapon
            if (const WeaponDef* wd = weaponDef(p.weapons[p.curWeapon].id)) {
                if (wd->ammo == slots[i].id) {
                    float magFrac = p.weapons[p.curWeapon].data / (float)magSizeOf(p.weapons[p.curWeapon]);
                    UI::bar(sx + 2, sy + SLOT - 4, SLOT - 4, 2, magFrac, P_YELLOW);
                }
            }
        }
        // Usage bar for armor durability
        if (itemDef(slots[i].id).cat == Cat::Armor && slots[i].data > 0) {
            const ItemDef& d = itemDef(slots[i].id);
            float frac = slots[i].data / (float)d.param;
            UI::bar(sx + 2, sy + SLOT - 4, SLOT - 4, 2, frac, P_BLUE);
        }

        if (isOther) {
            if (click == 2) { equipFrom(slots, i); continue; }
            const ItemDef& d = itemDef(slots[i].id);
            int lootedId = slots[i].id;
            bool autoEquip = (d.cat == Cat::Weapon && p.autoEquip && (p.weapons[0].empty() || p.weapons[1].empty())) ||
                             (d.cat == Cat::Melee && p.autoEquip && p.melee.empty()) ||
                             (d.cat == Cat::Armor && p.armor.empty()) || (d.cat == Cat::Backpack && p.backpack.empty());
            if (autoEquip && equipFrom(slots, i)) { missionAddLoot(lootedId); continue; }
            if (moveItem(slots, i, p.inv, p.invCapacity()) > 0) {
                Audio::play(Snd::pickup, 0.5f);
                missionAddLoot(lootedId);
            }
            else { pushMessage(T("Inventory full"), P_CORAL); setNotice(T("Inventory full.")); }
            continue;
        }
        switch (mode) {
        case InvMode::Raid:
            if (click == 1) useItemAt(slots, i);
            else { raid_dropItem(slots[i]); slots[i] = Item(); }
            break;
        case InvMode::Base:
            useItemAt(slots, i);
            break;
        case InvMode::Loot:
        case InvMode::Stash:
            if (click == 2) useItemAt(slots, i);
            else if (g_other) {
                if (moveItem(slots, i, *g_other, g_otherSlots) > 0) Audio::play(Snd::pickup, 0.4f, 0.8f);
                else setNotice(mode == InvMode::Stash ? T("Stash is full.") : T("Container is full."));
            }
            break;
        case InvMode::Trader:
            if (click == 1) {
                int v = itemValue(slots[i]);
                p.money += v;
                p.earned += v;
                setNotice(T2("Sold {0} for ${1}", itemLabel(slots[i]), std::to_string(v)));
                slots[i] = Item();
                Audio::play(Snd::sell, 0.6f);
            } else {
                useItemAt(slots, i);
            }
            break;
        }
    }
}

void drawInventoryPanel(float x, float y, InvMode mode, std::vector<Item>* other, int otherSlots) {
    Profile& p = G.prof;
    g_other = other;
    g_otherSlots = otherSlots;
    int cap = p.invCapacity();
    const int cols = 6;
    int rows = (cap + cols - 1) / cols;
    float w = cols * SLOT + 12;
    const float eqCell = (w - 12) / 3;      // three equipment slots per row: the guns and melee, then armour and pack
    const float eqRow = 30;
    const float gridY = 124;                // where the carried items start, clear of the Sort row and the auto-equip box
    bool pad = Input::usingPad();
    float h = gridY + rows * SLOT + 26 + (pad ? 16 : 0);   // a controller gets a second row of hints
    UI::panel(x, y, w, h, T("INVENTORY"));
    std::string money = "$" + std::to_string(p.money);
    R::text(money, x + w - 6 - R::textWidth(money), y + 4, pal(P_YGREEN));

    // Laid out GUN1 GUN2 MELEE / ARMOR PACK; the slot numbers stay 0-1 guns, 2 armour,
    // 3 pack, 4 melee.
    const char* labels[5] = {"GUN1", "GUN2", "ARMOR", "PACK", "MELEE"};
    Item* eq[5] = {&p.weapons[0], &p.weapons[1], &p.armor, &p.backpack, &p.melee};
    const int cellOf[5] = {0, 1, 3, 4, 2};
    for (int i = 0; i < 5; i++) {
        float cellX = x + 6 + (cellOf[i] % 3) * eqCell;
        float sx = std::floor(cellX + (eqCell - SLOT) / 2), sy = y + 18 + (cellOf[i] / 3) * eqRow;
        int click = UI::itemSlot(sx, sy, *eq[i], i < 2 && i == p.curWeapon && !eq[i]->empty());
        std::string lab = T(labels[i]);
        R::text(lab, std::floor(cellX + (eqCell - R::textWidth(lab)) / 2), sy + SLOT + 2, pal(P_LAVENDER));
        if (g_padDrag && UI::hover(sx, sy, SLOT, SLOT) && (click == 1 || Input::padGrabPressed())) {
            // A controller's carried item, put on: into that hand, on your back, worn.
            if (!dropDraggedOnEquipment(i)) setNotice(T("That does not go there."));
            endDrag();
            continue;
        }
        if (g_dragSlots && Input::mouseReleased(0) && UI::hover(sx, sy, SLOT, SLOT)) {
            dropDraggedOnEquipment(i);
            g_dragSlots = nullptr; g_dragIndex = -1; g_dragCountsMission = false;
            click = 0;
        }
        if (click == 1) {
            if (mode == InvMode::Loot || mode == InvMode::Stash) unequip(i, other, otherSlots);
            else unequip(i);
        } else if (click == 2 && i < 2) {
            p.curWeapon = i;
            G.player.reloadT = 0;
        }
    }

    // Armour condition is already on its slot as a bar, so only health and capacity
    // need spelling out here.
    float by = y + 18 + 2 * eqRow + 4;
    char buf[64];
    std::snprintf(buf, sizeof buf, "HP %d/%d", (int)p.hp, (int)p.maxHp());
    R::text(buf, x + 6, by, pal(P_CORAL));
    // And in the pack's small hearts, five for a full bar (0.12v).
    {
        float frac = clampf(p.hp / p.maxHp(), 0, 1), hx = x + 10 + R::textWidth(buf);
        for (int i = 0; i < 5; i++) {
            float f = clampf(frac * 5 - i, 0, 1);
            UI::skinSprite(f >= 0.75f ? "hp/small/heart_small_full" : f >= 0.25f ? "hp/small/heart_small_half" : "hp/small/heart_small_empty",
                           hx + i * 9, by - 2);
        }
    }
    R::text(T2("{0}/{1} slots", std::to_string(usedInvSlots()), std::to_string(cap)), x + 6, by + 10, pal(P_PURPLE));
    float sortW = 56, sortX = x + w - 6 - sortW;
    if (UI::hover(sortX, by + 9, sortW, 12)) UI::tooltip(T("Sort"), T("Sort the inventory by kind and value"));
    if (UI::button(sortX, by + 9, sortW, 12, T("Sort"))) {
        sortInventory();
        setNotice(T("Inventory sorted."));
        Audio::play(Snd::click, 0.5f, 1.2f);
    }

    // Guns you pick up go straight into an empty gun slot, unless you would rather
    // put them there yourself (0.12v).
    {
        std::string lab = T("Auto-equip weapons");
        if (UI::checkbox(x + 7, y + gridY - 12, p.autoEquip, lab)) { save_game(); }
        if (UI::hover(x + 5, y + gridY - 14, R::textWidth(lab) + 14, 11))
            UI::tooltip(lab, T("On: a gun or melee weapon you loot goes straight into an empty slot for it. Off: it goes in your pockets."));
    }

    drawSlotGrid(x + 6, y + gridY, p.inv, cap, cols, mode, false);

    if (g_dragSlots && g_dragIndex >= 0 && g_dragIndex < (int)g_dragSlots->size() && !(*g_dragSlots)[g_dragIndex].empty()) {
        Vec2 m = Input::mouse();
        // Carried with a controller: beside the cursor, so the slot under it still shows.
        if (g_padDrag) UI::itemIcon((*g_dragSlots)[g_dragIndex].id, m.x + 4, m.y + 4, 16, pal(P_WHITE, 0.9f));
        else UI::itemIcon((*g_dragSlots)[g_dragIndex].id, m.x - 8, m.y - 8, 16, pal(P_WHITE, 0.85f));
    }
    if (g_padDrag && g_dragSlots && g_dragIndex >= 0 && g_dragIndex < (int)g_dragSlots->size() && (*g_dragSlots)[g_dragIndex].empty()) endDrag();
    if (g_dragSlots && !g_padDrag && Input::mouseReleased(0)) endDrag();

    // What the two clicks do here, on their buttons.
    const char* a = "", *b = "";
    switch (mode) {
    case InvMode::Raid: a = "use/equip"; b = "drop"; break;
    case InvMode::Base: a = "use/equip"; break;
    case InvMode::Loot: a = "store"; b = "use/equip"; break;
    case InvMode::Stash: a = "stash"; b = "use/equip"; break;
    case InvMode::Trader: a = "SELL"; b = "use/equip"; break;
    }
    float hx = x + 5, hy = y + h - 18 - (pad ? 16 : 0);
    hx += Prompt::label(Prompt::Select, T(a), hx, hy, pal(P_LAVENDER)) + 6;
    if (*b) hx += Prompt::label(Prompt::AltSelect, T(b), hx, hy, pal(P_LAVENDER)) + 6;
    // A controller moves things with Y: pick up, then put down.
    if (pad) Prompt::label(Prompt::Grab, g_padDrag ? T("put it here") : T("move"), x + 5, y + h - 18, pal(g_padDrag ? P_YELLOW : P_LAVENDER));
}
