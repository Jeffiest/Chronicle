// Mod framework: game data tables from JSON (Windows fork).
//
//   mods/<mod>/data/*.json    sparse overrides, applied once at startup in mod load order, files alphabetically
//   DC_DUMP_DATA=1            writes every table as it is (after all mods) to mods/_dump/data/<table>.json
//
// Tables: weapons, items, attachments (keyed by item id), monsters (keyed by MonstorTable index), shops (shop number ->
// item id list). Only the keys a mod names are changed. See win-save/mods/DATA.md.

#include "moddata.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include "itemdata.hpp"
#include "monstorunit.hpp"
#include "platform/paths.hpp"
#include "shop.hpp"

extern WEAPON_DATA WeaponList[120];
extern ATTACH_DATA AttachList[50];
extern ITEM_DATA   ITEM_LIST[175];

// shop.cpp keeps the buy/sell price of every item from ITEM_ATTACH_START (81) on in a global; its entry type is private to that file.
struct ModPrice {
    s16 buy;
    s16 sell;
};
extern ModPrice PriceList[296];

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr int kMonsterKinds = 167;
constexpr int kShops = 18;
constexpr int kShopSlots = 20;

std::string Utf8(const fs::path &p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// ---- field <-> JSON ---------------------------------------------------------------------------------------------------

template <class T> json ToJson(const T &v) {
    if constexpr (std::is_floating_point_v<T>) {
        return v;
    } else {
        return static_cast<long long>(v);
    }
}
template <class T, size_t N> json ToJson(const T (&a)[N]) {
    json out = json::array();
    for (size_t i = 0; i < N; i++) {
        out.push_back(ToJson(a[i]));
    }
    return out;
}

template <class T> bool FromJson(T &dst, const json &v) {
    if (!v.is_number()) {
        return false;
    }
    double d = v.get<double>();
    if constexpr (std::is_floating_point_v<T>) {
        dst = static_cast<T>(d);
        return true;
    } else {
        double lo = static_cast<double>(std::numeric_limits<T>::min());
        double hi = static_cast<double>(std::numeric_limits<T>::max());
        if (std::isnan(d) || d < lo || d > hi) {
            return false;
        }
        dst = static_cast<T>(std::llround(d));
        return true;
    }
}
template <class T, size_t N> bool FromJson(T (&dst)[N], const json &v) {
    if (!v.is_array() || v.size() > N) {
        return false;
    }
    T tmp[N];
    for (size_t i = 0; i < N; i++) {
        tmp[i] = dst[i];
    }
    for (size_t i = 0; i < v.size(); i++) {
        if (!FromJson(tmp[i], v[i])) {
            return false;
        }
    }
    for (size_t i = 0; i < N; i++) {
        dst[i] = tmp[i];
    }
    return true;
}

// Each table lists its named fields once; a visitor either reads them (dump) or writes them (override).
struct Dumper {
    json &out;
    template <class T> void operator()(const char *name, T &field) { out[name] = ToJson(field); }
};
struct Setter {
    const std::string &key;
    const json        &value;
    bool               known = false;
    bool               ok = false;
    template <class T> void operator()(const char *name, T &field) {
        if (key == name) {
            known = true;
            ok = FromJson(field, value);
        }
    }
};

template <class V> void WeaponFields(WEAPON_DATA &p, V &&f) {
    f("durability", p.durability);
    f("attack", p.attack);
    f("endurance", p.endurance);
    f("speed", p.speed);
    f("magic", p.magic);
    f("owner", p.owner);
    f("hole", p.hole);
    f("hole_num", p.hole_num);
    f("elem", p.elem);
    f("vs_monster", p.vs_monster);
    f("exp_base", p.exp_base);
    f("exp_per_level", p.exp_per_level);
    f("flags", p.flags);
    f("buildup_mask0", p.buildup_mask0);
    f("buildup_mask1", p.buildup_mask1);
    f("attack_max", p.attack_max);
    f("magic_max", p.magic_max);
    f("chain_pos", p.chain_pos);
}
template <class V> void ItemFields(ITEM_DATA &p, V &&f) {
    f("sort_key", p.sort_key);
    f("use_flags", p.use_flags);
    f("kind_flags", p.kind_flags);
    f("vol", p.vol);
    f("vol_range", p.vol_range);
    f("stack_kind", p.stack_kind);
}
template <class V> void AttachFields(ATTACH_DATA &p, V &&f) {
    f("sphere_weapon_no", p.sphere_weapon_no);
    f("sphere_flags", p.sphere_flags);
    f("sphere_level", p.sphere_level);
    f("attack", p.attack);
    f("endurance", p.endurance);
    f("speed", p.speed);
    f("magic", p.magic);
    f("elem", p.elem);
    f("vs_monster", p.vs_monster);
}
template <class V> void PriceFields(ModPrice &p, V &&f) {
    f("buy", p.buy);
    f("sell", p.sell);
}
template <class V> void MonsterFields(MONSTOR_MODEL &p, V &&f) {
    f("hp", p.max_hp);
    f("attachment_kind", p.attachment_kind);
    f("attachment_weight", p.attachment_weight);
    f("collision_radius", p.collision_radius);
    f("defense", p.defense);
    f("hardness", p.hardness);
    f("shot_effect", p.shot_effect);
    f("exp", p.exp);
    f("money", p.money);
    f("money_chance", p.money_chance);
    f("kind", p.kind);
    f("name_no", p.name_no);
    f("steal_item", p.steal_item);
    f("drops_items", p.drops_items);
    f("item_damage_rate", p.item_damage_rate);
    f("status_chance", p.status_chance);
    f("rare_item", p.rare_item);
    f("damage_from_attacker", p.effect_parameter);
    f("knockback_scale", p.knockback_scale);
}

void Warn(const std::string &mod, const std::string &text) {
    std::fprintf(stderr, "[mod %s] data: %s\n", mod.c_str(), text.c_str());
}

bool ParseId(const std::string &key, int &id) {
    if (key.empty() || key.size() > 6 || key.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    id = std::stoi(key);
    return true;
}

// Applies {"<id>": {field: value, ...}, ...} to the object `get(id)` returns.
template <class Obj, class Fields, class Get>
int ApplySection(const std::string &mod, const char *table, const json &section, Get get, Fields fields) {
    int changed = 0;
    if (!section.is_object()) {
        Warn(mod, std::string(table) + " must be an object of id -> fields");
        return 0;
    }
    for (auto it = section.begin(); it != section.end(); ++it) {
        int id = 0;
        Obj *obj = nullptr;
        if (!ParseId(it.key(), id) || (obj = get(id)) == nullptr) {
            Warn(mod, std::string(table) + ": no entry '" + it.key() + "'; skipped");
            continue;
        }
        if (!it.value().is_object()) {
            Warn(mod, std::string(table) + " " + it.key() + ": expected an object of fields");
            continue;
        }
        for (auto f = it.value().begin(); f != it.value().end(); ++f) {
            if (f.key().empty() || f.key()[0] == '_') {
                continue; // comment keys; the dump labels monsters with _model
            }
            Setter s{f.key(), f.value()};
            fields(*obj, s);
            if (!s.known) {
                Warn(mod, std::string(table) + " " + it.key() + ": unknown field '" + f.key() + "'");
            } else if (!s.ok) {
                Warn(mod, std::string(table) + " " + it.key() + "." + f.key() + ": bad value (wrong type, too long or out of range)");
            } else {
                changed++;
            }
        }
    }
    return changed;
}

int ApplyShops(const std::string &mod, const json &section) {
    int changed = 0;
    if (!section.is_object()) {
        Warn(mod, "shops must be an object of shop number -> [item ids]");
        return 0;
    }
    for (auto it = section.begin(); it != section.end(); ++it) {
        int shop = 0;
        if (!ParseId(it.key(), shop) || shop >= kShops || !it.value().is_array() || it.value().size() > kShopSlots) {
            Warn(mod, "shops: '" + it.key() + "' must be a shop number 0-17 with a list of up to 20 item ids; skipped");
            continue;
        }
        s16 list[kShopSlots];
        std::fill(std::begin(list), std::end(list), static_cast<s16>(-1));
        bool good = true;
        for (size_t i = 0; i < it.value().size() && good; i++) {
            int id = -1;
            good = FromJson(id, it.value()[i]) && id > 0 && id < 400;
            list[i] = static_cast<s16>(id);
        }
        if (!good) {
            Warn(mod, "shops " + it.key() + ": item ids must be numbers from 1 to 399; skipped");
            continue;
        }
        s16 *dst = GetItemShopList(shop);
        for (int i = 0; i < kShopSlots; i++) {
            dst[i] = list[i];
        }
        changed++;
    }
    return changed;
}

// Pretty-prints with every array of numbers on one line, so a dumped table stays readable.
void Pretty(const json &j, std::string &out, int depth) {
    if (j.is_object()) {
        out += "{\n";
        size_t index = 0;
        for (auto it = j.begin(); it != j.end(); ++it, ++index) {
            out.append(static_cast<size_t>(depth + 1), ' ');
            out += json(it.key()).dump() + ": ";
            Pretty(it.value(), out, depth + 1);
            out += index + 1 < j.size() ? ",\n" : "\n";
        }
        out.append(static_cast<size_t>(depth), ' ');
        out += "}";
    } else {
        out += j.dump(); // arrays and numbers: one line
    }
}

json ReadJsonFile(const fs::path &file, const std::string &mod) {
    std::ifstream in(file);
    json out = in ? json::parse(in, nullptr, false, true) : json();
    if (out.is_discarded() || !out.is_object()) {
        Warn(mod, Utf8(file.filename()) + " is not a valid JSON object; ignored");
        return json::object();
    }
    return out;
}

} // namespace

void ModDataApply(const std::filesystem::path &mod_dir, const std::string &mod) {
    std::error_code error;
    fs::path        dir = mod_dir / "data";
    if (!fs::is_directory(dir, error)) {
        return;
    }
    std::vector<fs::path> files;
    for (const auto &entry : fs::directory_iterator(dir, error)) {
        if (entry.is_regular_file(error) && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const fs::path &file : files) {
        json root = ReadJsonFile(file, mod);
        int  n = 0;
        for (auto it = root.begin(); it != root.end(); ++it) {
            const std::string &t = it.key();
            if (t == "weapons") {
                n += ApplySection<WEAPON_DATA>(mod, "weapons", it.value(), [](int id) { return GetWeaponData(id); },
                                               [](WEAPON_DATA &p, Setter &s) { WeaponFields(p, s); });
            } else if (t == "items") {
                n += ApplySection<ITEM_DATA>(mod, "items", it.value(), [](int id) { return GetItemData(id); },
                                             [](ITEM_DATA &p, Setter &s) { ItemFields(p, s); });
            } else if (t == "attachments") {
                n += ApplySection<ATTACH_DATA>(mod, "attachments", it.value(), [](int id) { return GetAttachData(id); },
                                               [](ATTACH_DATA &p, Setter &s) { AttachFields(p, s); });
            } else if (t == "monsters") {
                n += ApplySection<MONSTOR_MODEL>(mod, "monsters", it.value(),
                                                 [](int id) { return id < kMonsterKinds ? &MonstorTable[id] : nullptr; },
                                                 [](MONSTOR_MODEL &p, Setter &s) { MonsterFields(p, s); });
            } else if (t == "prices") {
                n += ApplySection<ModPrice>(mod, "prices", it.value(),
                                            [](int id) { return id >= 81 && id < 81 + 296 ? &PriceList[id - 81] : nullptr; },
                                            [](ModPrice &p, Setter &s) { PriceFields(p, s); });
            } else if (t == "shops") {
                n += ApplyShops(mod, it.value());
            } else if (t[0] != '_') { // keys starting with _ are comments
                Warn(mod, Utf8(file.filename()) + ": unknown table '" + t + "'");
            }
        }
        std::fprintf(stderr, "[mod %s] data: %s: %d value(s) applied\n", mod.c_str(), Utf8(file.filename()).c_str(), n);
    }
}

void ModDataDump(const std::filesystem::path &out_dir) {
    std::error_code error;
    fs::create_directories(out_dir, error);
    auto write = [&](const char *name, const json &j) {
        std::string text;
        Pretty(j, text, 0);
        std::ofstream out(out_dir / (std::string(name) + ".json"));
        out << text << "\n";
    };
    json weapons = json::object(), items = json::object(), attachments = json::object(), monsters = json::object(),
         shops = json::object();
    for (int id = 1; id < 400; id++) {
        if (WEAPON_DATA *w = GetWeaponData(id)) {
            WeaponFields(*w, Dumper{weapons[std::to_string(id)]});
        }
        if (ITEM_DATA *i = GetItemData(id)) {
            ItemFields(*i, Dumper{items[std::to_string(id)]});
        }
        if (ATTACH_DATA *a = GetAttachData(id)) {
            AttachFields(*a, Dumper{attachments[std::to_string(id)]});
        }
    }
    json prices = json::object();
    for (int id = 81; id < 81 + 296; id++) {
        PriceFields(PriceList[id - 81], Dumper{prices[std::to_string(id)]});
    }
    write("prices", prices);
    for (int k = 0; k < kMonsterKinds; k++) {
        MonsterFields(MonstorTable[k], Dumper{monsters[std::to_string(k)]});
        monsters[std::to_string(k)]["_model"] = MonstorTable[k].model_name[0];
    }
    for (int s = 0; s < kShops; s++) {
        json list = json::array();
        s16 *p = GetItemShopList(s);
        for (int i = 0; i < kShopSlots && p[i] != -1; i++) {
            list.push_back(p[i]);
        }
        shops[std::to_string(s)] = list;
    }
    write("weapons", weapons);
    write("items", items);
    write("attachments", attachments);
    write("monsters", monsters);
    write("shops", shops);
    std::fprintf(stderr, "data tables dumped to %s\n", Utf8(out_dir).c_str());
}
