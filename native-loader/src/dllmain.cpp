#include <Mod/CppUserModBase.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/UFunction.hpp>

#include <array>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace
{
    using RC::Unreal::UFunction;
    using RC::Unreal::TCHAR;
    using RC::Unreal::UObject;

    constexpr std::int32_t kMaxPartySlots = 5;

    struct PendingBoxMove
    {
        UObject* Player{};
        UObject* PreviousPartyPal{};
        std::int32_t Slot{-1};
    };

    std::mutex g_pending_box_moves_mutex;
    std::unordered_map<std::uintptr_t, PendingBoxMove> g_pending_box_moves;

    std::atomic<std::uint64_t> g_respawn_events{0};
    std::atomic<std::uint64_t> g_base_entry_events{0};
    std::atomic<std::uint64_t> g_box_events{0};
    std::atomic<std::uint32_t> g_ownership_warnings{0};
    std::atomic<std::uint32_t> g_candidate_probe_hits{0};

    UFunction* find_function(UObject* context, const TCHAR* name)
    {
        if (context == nullptr)
        {
            return nullptr;
        }

        if (auto* function = context->GetFunctionByName(name); function != nullptr)
        {
            return function;
        }

        return context->GetFunctionByNameInChain(name);
    }

    bool has_function(UObject* context, const TCHAR* name)
    {
        return find_function(context, name) != nullptr;
    }

    bool is_named(UFunction* function, const TCHAR* name)
    {
        return function != nullptr && function->GetName() == name;
    }

    bool prepare_call(UObject* context, const TCHAR* name, UFunction*& function, std::vector<std::uint8_t>& params)
    {
        function = find_function(context, name);
        if (function == nullptr)
        {
            return false;
        }

        const auto params_size = function->GetParmsSize();
        if (params_size < 0 || params_size > 1024 * 1024)
        {
            return false;
        }

        params.assign(static_cast<std::size_t>(params_size == 0 ? 1 : params_size), 0);
        context->ProcessEvent(function, params.data());
        return true;
    }

    UObject* call_object(UObject* context, const TCHAR* name)
    {
        UFunction* function = nullptr;
        std::vector<std::uint8_t> params;
        if (!prepare_call(context, name, function, params))
        {
            return nullptr;
        }

        const auto return_offset = function->GetReturnValueOffset();
        if (return_offset < 0 || static_cast<std::size_t>(return_offset) + sizeof(UObject*) > params.size())
        {
            return nullptr;
        }

        UObject* result = nullptr;
        std::memcpy(&result, params.data() + return_offset, sizeof(result));
        return result;
    }

    UObject* call_object_index(UObject* context, const TCHAR* name, std::int32_t index)
    {
        UFunction* function = find_function(context, name);
        if (function == nullptr)
        {
            return nullptr;
        }

        const auto params_size = function->GetParmsSize();
        if (params_size < static_cast<std::int32_t>(sizeof(index)) || params_size > 1024 * 1024)
        {
            return nullptr;
        }

        std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
        std::memcpy(params.data(), &index, sizeof(index));
        context->ProcessEvent(function, params.data());

        const auto return_offset = function->GetReturnValueOffset();
        if (return_offset < 0 || static_cast<std::size_t>(return_offset) + sizeof(UObject*) > params.size())
        {
            return nullptr;
        }

        UObject* result = nullptr;
        std::memcpy(&result, params.data() + return_offset, sizeof(result));
        return result;
    }

    bool call_bool(UObject* context, const TCHAR* name, bool& found)
    {
        found = false;
        UFunction* function = nullptr;
        std::vector<std::uint8_t> params;
        if (!prepare_call(context, name, function, params))
        {
            return false;
        }

        const auto return_offset = function->GetReturnValueOffset();
        if (return_offset < 0 || static_cast<std::size_t>(return_offset) >= params.size())
        {
            return false;
        }

        found = true;
        return params[static_cast<std::size_t>(return_offset)] != 0;
    }

    bool call_void(UObject* context, const TCHAR* name)
    {
        UFunction* function = nullptr;
        std::vector<std::uint8_t> params;
        return prepare_call(context, name, function, params);
    }

    UObject* get_player_state(UObject* player)
    {
        for (const auto* name : {STR("GetPalPlayerState"), STR("GetPlayerState")})
        {
            if (auto* state = call_object(player, name); state != nullptr)
            {
                return state;
            }
        }
        return nullptr;
    }

    bool looks_like_player(UObject* candidate)
    {
        if (candidate == nullptr)
        {
            return false;
        }

        return has_function(candidate, STR("GetPalPlayerOtomoData"))
            || has_function(candidate, STR("GetOtomoActorBySlotIndex"))
            || has_function(candidate, STR("GetPalPlayerState"));
    }

    UObject* resolve_player(UObject* context)
    {
        if (looks_like_player(context))
        {
            return context;
        }

        for (const auto* name : {
                 STR("GetOwnerPlayer"),
                 STR("GetPlayerCharacter"),
                 STR("GetPalPlayerCharacter"),
                 STR("GetPlayerControlledCharacter"),
                 STR("GetOwner"),
             })
        {
            if (auto* player = call_object(context, name); looks_like_player(player))
            {
                return player;
            }
        }

        return nullptr;
    }

    std::vector<UObject*> get_party_pals(UObject* player)
    {
        std::vector<UObject*> roots;
        roots.push_back(player);

        if (auto* state = get_player_state(player); state != nullptr)
        {
            roots.push_back(state);
        }

        for (UObject* root : {player, get_player_state(player)})
        {
            if (root == nullptr)
            {
                continue;
            }

            for (const auto* name : {
                     STR("GetPalPlayerOtomoData"),
                     STR("GetOtomoHolderComponent"),
                     STR("GetOtomoHolder"),
                 })
            {
                if (auto* object = call_object(root, name); object != nullptr)
                {
                    roots.push_back(object);
                }
            }
        }

        std::vector<UObject*> result;
        for (UObject* root : roots)
        {
            if (root == nullptr)
            {
                continue;
            }

            for (std::int32_t slot = 0; slot < kMaxPartySlots; ++slot)
            {
                for (const auto* name : {
                         STR("GetOtomoActorBySlotIndex"),
                     })
                {
                    if (auto* pal = call_object_index(root, name, slot); pal != nullptr)
                    {
                        if (std::find(result.begin(), result.end(), pal) == result.end())
                        {
                            result.push_back(pal);
                        }
                        break;
                    }
                }
            }

            if (!result.empty())
            {
                break;
            }
        }

        return result;
    }

    UObject* get_party_pal_at_slot(UObject* player, std::int32_t slot)
    {
        if (player == nullptr || slot < 0 || slot >= kMaxPartySlots)
        {
            return nullptr;
        }

        std::vector<UObject*> roots;
        roots.push_back(player);
        if (auto* state = get_player_state(player); state != nullptr)
        {
            roots.push_back(state);
        }

        for (UObject* root : {player, get_player_state(player)})
        {
            if (root == nullptr)
            {
                continue;
            }

            for (const auto* name : {
                     STR("GetPalPlayerOtomoData"),
                     STR("GetOtomoHolderComponent"),
                     STR("GetOtomoHolder"),
                 })
            {
                if (auto* object = call_object(root, name); object != nullptr)
                {
                    roots.push_back(object);
                }
            }
        }

        for (UObject* root : roots)
        {
            if (root == nullptr)
            {
                continue;
            }

            for (const auto* name : {
                     STR("GetOtomoActorBySlotIndex"),
                 })
            {
                if (auto* pal = call_object_index(root, name, slot); pal != nullptr)
                {
                    return pal;
                }
            }
        }

        return nullptr;
    }

    UObject* get_parameter_component(UObject* pal)
    {
        for (const auto* name : {
                 STR("GetCharacterParameterComponent"),
                 STR("GetCharacterParameter"),
                 STR("GetIndividualCharacterParameter"),
             })
        {
            if (auto* component = call_object(pal, name); component != nullptr)
            {
                return component;
            }
        }
        return nullptr;
    }

    bool revive_if_downed(UObject* pal)
    {
        if (pal == nullptr)
        {
            return false;
        }

        std::array<UObject*, 2> candidates{pal, get_parameter_component(pal)};
        for (UObject* candidate : candidates)
        {
            if (candidate == nullptr || !has_function(candidate, STR("IsDying"))
                || !has_function(candidate, STR("IsDyingHPZero")))
            {
                continue;
            }

            bool dying_found = false;
            bool hp_zero_found = false;
            const bool is_dying = call_bool(candidate, STR("IsDying"), dying_found);
            const bool is_hp_zero = call_bool(candidate, STR("IsDyingHPZero"), hp_zero_found);
            if (!dying_found || !hp_zero_found || !is_dying || !is_hp_zero)
            {
                continue;
            }

            if (has_function(candidate, STR("ReviveFromDying"))
                && call_void(candidate, STR("ReviveFromDying")))
            {
                RC::Output::send(STR("[PalworldNative] ReviveFromDying: {}\n"), pal->GetName());
                return true;
            }
        }

        return false;
    }

    std::size_t revive_party(UObject* player)
    {
        if (player == nullptr)
        {
            return 0;
        }

        std::size_t revived = 0;
        for (UObject* pal : get_party_pals(player))
        {
            if (revive_if_downed(pal))
            {
                ++revived;
            }
        }
        return revived;
    }

    UObject* pointer_at(const void* params, std::size_t params_size, std::size_t offset)
    {
        if (params == nullptr || offset + sizeof(UObject*) > params_size)
        {
            return nullptr;
        }

        UObject* result = nullptr;
        std::memcpy(&result, static_cast<const std::uint8_t*>(params) + offset, sizeof(result));
        return result;
    }

    std::int32_t int32_at(const void* params, std::size_t params_size, std::size_t offset)
    {
        std::int32_t result = -1;
        if (params != nullptr && offset + sizeof(result) <= params_size)
        {
            std::memcpy(&result, static_cast<const std::uint8_t*>(params) + offset, sizeof(result));
        }
        return result;
    }

    UObject* base_camp_from_event(UFunction* function, UObject* context, const void* params, std::size_t params_size)
    {
        if (is_named(function, STR("OnPlayerEnterBaseCamp")))
        {
            if (auto* camp = pointer_at(params, params_size, 16); camp != nullptr)
            {
                return camp;
            }
            return pointer_at(params, params_size, 8);
        }

        if (auto* camp = pointer_at(params, params_size, 8); camp != nullptr)
        {
            return camp;
        }
        return pointer_at(params, params_size, 16);
    }

    UObject* player_from_base_event(UFunction* function, UObject* context, const void* params, std::size_t params_size)
    {
        if (auto* player = resolve_player(context); player != nullptr)
        {
            return player;
        }

        if (is_named(function, STR("OnPlayerEnterBaseCamp")))
        {
            if (auto* player = pointer_at(params, params_size, 8); looks_like_player(player))
            {
                return player;
            }
            return pointer_at(params, params_size, 0);
        }

        if (auto* player = pointer_at(params, params_size, 0); looks_like_player(player))
        {
            return player;
        }
        return pointer_at(params, params_size, 8);
    }

    std::optional<std::array<std::uint8_t, 16>> call_guid(UObject* context, const TCHAR* name)
    {
        UFunction* function = nullptr;
        std::vector<std::uint8_t> params;
        if (!prepare_call(context, name, function, params))
        {
            return std::nullopt;
        }

        const auto return_offset = function->GetReturnValueOffset();
        if (return_offset < 0 || static_cast<std::size_t>(return_offset) + 16 > params.size())
        {
            return std::nullopt;
        }

        std::array<std::uint8_t, 16> value{};
        std::memcpy(value.data(), params.data() + return_offset, value.size());
        return value;
    }

    bool nonzero_guid(const std::array<std::uint8_t, 16>& value)
    {
        for (const auto byte : value)
        {
            if (byte != 0)
            {
                return true;
            }
        }
        return false;
    }

    bool same_guild(UObject* player, UObject* base_camp)
    {
        std::vector<UObject*> player_roots{player};
        if (auto* state = get_player_state(player); state != nullptr)
        {
            player_roots.push_back(state);
        }

        std::vector<UObject*> camp_roots{base_camp};
        for (const auto* name : {STR("GetBaseCampModel"), STR("GetBaseCampModelBelongTo")})
        {
            if (auto* model = call_object(base_camp, name); model != nullptr)
            {
                camp_roots.push_back(model);
            }
        }

        for (UObject* player_root : player_roots)
        {
            for (const auto* player_name : {
                     STR("GetGuildBelongTo"),
                     STR("GetMyGuild"),
                     STR("GetGuild"),
                 })
            {
                if (auto* player_guild = call_object(player_root, player_name); player_guild != nullptr)
                {
                    for (UObject* camp_root : camp_roots)
                    {
                        for (const auto* camp_name : {
                                 STR("GetBaseCampBelongTo"),
                                 STR("GetGuildBelongTo"),
                                 STR("GetGuild"),
                                 STR("GetGroup"),
                             })
                        {
                            if (auto* camp_guild = call_object(camp_root, camp_name); camp_guild != nullptr
                                && camp_guild == player_guild)
                            {
                                return true;
                            }
                        }
                    }
                }
            }
        }

        for (UObject* player_root : player_roots)
        {
            for (const auto* player_name : {
                     STR("GetGuildId"),
                     STR("GetGroupId"),
                     STR("GetLocalPlayerGroupID"),
                 })
            {
                const auto player_id = call_guid(player_root, player_name);
                if (!player_id.has_value() || !nonzero_guid(*player_id))
                {
                    continue;
                }

                for (UObject* camp_root : camp_roots)
                {
                    for (const auto* camp_name : {
                             STR("GetGroupIdBelongTo"),
                             STR("GetBaseCampIdBelongTo"),
                             STR("GetBaseCampID"),
                             STR("GetBaseCampId"),
                         })
                    {
                        const auto camp_id = call_guid(camp_root, camp_name);
                        if (camp_id.has_value() && nonzero_guid(*camp_id) && *camp_id == *player_id)
                        {
                            return true;
                        }
                    }
                }
            }
        }

        return false;
    }

    bool is_box_event(UFunction* function)
    {
        return is_named(function, STR("SetOtomoSlot"))
            || is_named(function, STR("SetOtomoSlot_ToServer"));
    }

    const TCHAR* candidate_event_name(UFunction* function)
    {
        for (const auto* name : {
                 STR("SetOtomoSlot"),
                 STR("SetOtomoSlot_ToServer"),
                 STR("PalBoxSlot_ToServer"),
                 STR("PalBox_ToServer"),
                 STR("PalStorage_ToServer"),
                 STR("NotifyRemovedCharacterFromPalBox_ToServer"),
                 STR("NotifyObtainComplete_ToServer"),
                 STR("CallRespawnDelegate"),
                 STR("RespawnReady"),
                 STR("OnPlayerEnterBaseCamp"),
                 STR("OnEnterBaseCamp"),
             })
        {
            if (is_named(function, name))
            {
                return name;
            }
        }
        return nullptr;
    }

    bool is_respawn_event(UFunction* function)
    {
        return is_named(function, STR("CallRespawnDelegate"))
            || is_named(function, STR("RespawnReady"));
    }

    bool is_base_entry_event(UFunction* function)
    {
        return is_named(function, STR("OnPlayerEnterBaseCamp"))
            || is_named(function, STR("OnEnterBaseCamp"));
    }

    void on_process_event_pre(UObject* context, UFunction* function, void* params)
    {
        if (const auto* candidate = candidate_event_name(function); candidate != nullptr)
        {
            const auto probe_index = g_candidate_probe_hits.fetch_add(1, std::memory_order_relaxed);
            if (probe_index < 16)
            {
                RC::Output::send(
                    STR("[PalworldNative] candidate event {} params={}\n"),
                    candidate,
                    function->GetParmsSize());
            }
        }

        if (!is_box_event(function))
        {
            return;
        }

        const auto params_size = static_cast<std::size_t>(function->GetParmsSize());
        const auto slot = int32_at(params, params_size, 0);
        if (slot < 0 || slot >= kMaxPartySlots)
        {
            return;
        }

        auto* player = resolve_player(context);
        if (player == nullptr)
        {
            return;
        }

        const auto previous = get_party_pal_at_slot(player, slot);

        std::lock_guard lock(g_pending_box_moves_mutex);
        g_pending_box_moves[reinterpret_cast<std::uintptr_t>(context)] = {player, previous, slot};
    }

    void on_process_event_post(UObject* context, UFunction* function, void* params)
    {
        if (is_box_event(function))
        {
            PendingBoxMove pending;
            {
                std::lock_guard lock(g_pending_box_moves_mutex);
                const auto it = g_pending_box_moves.find(reinterpret_cast<std::uintptr_t>(context));
                if (it == g_pending_box_moves.end())
                {
                    return;
                }
                pending = it->second;
                g_pending_box_moves.erase(it);
            }

            ++g_box_events;
            if (pending.PreviousPartyPal != nullptr)
            {
                revive_if_downed(pending.PreviousPartyPal);
            }
            return;
        }

        if (is_respawn_event(function))
        {
            auto* player = resolve_player(context);
            if (player == nullptr)
            {
                return;
            }

            ++g_respawn_events;
            const auto revived = revive_party(player);
            RC::Output::send(STR("[PalworldNative] respawn event: party revived={}\n"), revived);
            return;
        }

        if (is_base_entry_event(function))
        {
            const auto params_size = static_cast<std::size_t>(function->GetParmsSize());
            auto* player = player_from_base_event(function, context, params, params_size);
            auto* base_camp = base_camp_from_event(function, context, params, params_size);
            if (player == nullptr || base_camp == nullptr || !same_guild(player, base_camp))
            {
                const auto warning_index = g_ownership_warnings.fetch_add(1, std::memory_order_relaxed);
                if (warning_index < 3)
                {
                    RC::Output::send(STR("[PalworldNative] base entry ignored: own-guild identity unavailable.\n"));
                }
                return;
            }

            ++g_base_entry_events;
            const auto revived = revive_party(player);
            RC::Output::send(STR("[PalworldNative] own base entry: party revived={}\n"), revived);
        }
    }

    class PalworldServerAutoReviveNative final : public RC::CppUserModBase
    {
    public:
        PalworldServerAutoReviveNative()
        {
            ModName = STR("PalworldServerAutoReviveNative");
            ModAuthors = STR("p7672716");
            ModDescription = STR("Debian-native Palworld event bridge and standard revive layer");
            ModVersion = STR("0.2.0-native-events");
        }

        void on_program_start() override
        {
            RC::Unreal::Hook::RegisterProcessEventPreCallback(
                [](auto&, UObject* context, UFunction* function, void* params) {
                    on_process_event_pre(context, function, params);
                },
                {false, false, STR("PalworldServerAutoReviveNative"), STR("PalworldEventPre")});

            RC::Unreal::Hook::RegisterProcessEventPostCallback(
                [](auto&, UObject* context, UFunction* function, void* params) {
                    on_process_event_post(context, function, params);
                },
                {false, false, STR("PalworldServerAutoReviveNative"), STR("PalworldEventPost")});

            RC::Output::send(STR("[PalworldNative] event bridge registered: box/respawn/own-base.\n"));
        }
    };
}

extern "C"
{
    __attribute__((visibility("default"))) RC::CppUserModBase* start_mod()
    {
        return new PalworldServerAutoReviveNative();
    }

    __attribute__((visibility("default"))) void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}


