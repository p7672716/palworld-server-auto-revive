local MOD_NAME = "PalworldServerAutoRevive"
local ok_config, config = pcall(require, "config")
if not ok_config or type(config) ~= "table" then
    config = {}
end

local EVENT_DELAY_MS = config.event_delay_ms or 100
local DEBUG_LOGGING = config.debug_logging == true
local LOG_SKIPPED = config.log_skipped == true

local pending_box_moves = {}
local hook_status = {}

local function log(message)
    print("[" .. MOD_NAME .. "] " .. message)
end

local function debug(message)
    if DEBUG_LOGGING then
        log(message)
    end
end

local function safe_call(fn)
    local ok, result = pcall(fn)
    if ok then
        return result
    end
    return nil
end

local function unwrap(value)
    if value == nil then
        return nil
    end

    local unwrapped = safe_call(function()
        return value:get()
    end)
    if unwrapped ~= nil then
        return unwrapped
    end

    return value
end

local function valid(object)
    if object == nil then
        return false
    end

    local result = safe_call(function()
        return object:IsValid()
    end)
    return result == true
end

local function address(object)
    if not valid(object) then
        return nil
    end
    return safe_call(function()
        return object:GetAddress()
    end)
end

local function same_object(left, right)
    local left_address = address(left)
    local right_address = address(right)
    return left_address ~= nil and left_address == right_address
end

local function field(object, name)
    if not valid(object) then
        return nil
    end

    return safe_call(function()
        return object:GetPropertyValue(name)
    end)
end

local function guid_key(guid)
    if guid == nil then
        return nil
    end

    local text = safe_call(function()
        return guid:ToString()
    end)
    if text ~= nil then
        return text
    end

    local a = safe_call(function() return guid.A end)
    local b = safe_call(function() return guid.B end)
    local c = safe_call(function() return guid.C end)
    local d = safe_call(function() return guid.D end)
    if a ~= nil and b ~= nil and c ~= nil and d ~= nil then
        return string.format("%08x-%08x-%08x-%08x", a, b, c, d)
    end

    return tostring(guid)
end

local function id_from_container_id(container_id)
    if container_id == nil then
        return nil
    end
    return unwrap(safe_call(function()
        return container_id.ID
    end))
end

local function container_id_from_slot_id(slot_id)
    if slot_id == nil then
        return nil
    end

    local container_id = unwrap(safe_call(function()
        return slot_id.ContainerId
    end))
    return id_from_container_id(container_id)
end

local function slot_index_from_slot_id(slot_id)
    if slot_id == nil then
        return nil
    end
    return safe_call(function()
        return slot_id.SlotIndex
    end)
end

local function now_seconds()
    return os.clock()
end

local function schedule(callback)
    local function run()
        local ok, error_message = pcall(callback)
        if not ok then
            log("scheduled operation failed: " .. tostring(error_message))
        end
    end

    if ExecuteInGameThread ~= nil then
        ExecuteInGameThread(run)
    else
        ExecuteWithDelay(0, run)
    end
end

local function visit_array(array, callback)
    if array == nil then
        return
    end

    local for_each_ok = pcall(function()
        array:ForEach(function(_, element)
            callback(unwrap(element))
        end)
    end)
    if for_each_ok then
        return
    end

    if type(array) == "table" then
        for _, element in pairs(array) do
            callback(unwrap(element))
        end
    end
end

local function get_parameter(handle)
    if not valid(handle) then
        return nil
    end
    return unwrap(safe_call(function()
        return handle:TryGetIndividualParameter()
    end))
end

local function get_actor(handle, parameter)
    local actor = nil
    if valid(handle) then
        actor = unwrap(safe_call(function()
            return handle:TryGetIndividualActor()
        end))
    end

    if not valid(actor) and valid(parameter) then
        actor = unwrap(safe_call(function()
            return parameter:GetIndividualActor()
        end))
    end
    return actor
end

local function fixed_point_is_zero(value)
    value = unwrap(value)
    if value == nil then
        return false
    end

    if type(value) == "number" then
        return value == 0
    end

    local raw_value = unwrap(safe_call(function()
        return value.Value
    end))
    return type(raw_value) == "number" and raw_value == 0
end

local function is_incapacitated(actor)
    if not valid(actor) then
        return false
    end

    local component = unwrap(safe_call(function()
        return actor:GetCharacterParameterComponent()
    end))
    if not valid(component) then
        return false
    end

    local is_dead = safe_call(function()
        return component:IsDead()
    end)
    if is_dead == true then
        return false
    end

    local is_dying = safe_call(function()
        return component:IsDying()
    end)
    if is_dying ~= true then
        return false
    end

    local parameter = unwrap(field(component, "IndividualParameter"))
    if not valid(parameter) then
        return false
    end

    local hp = safe_call(function()
        return parameter:GetHP()
    end)
    return fixed_point_is_zero(hp)
end

local function revive_if_incapacitated(handle, event_name)
    if not valid(handle) then
        return false, "invalid_handle"
    end

    local parameter = get_parameter(handle)
    local actor = get_actor(handle, parameter)
    if not valid(actor) then
        return false, "actor_not_loaded"
    end

    if not is_incapacitated(actor) then
        return false, "not_incapacitated"
    end

    local component = unwrap(safe_call(function()
        return actor:GetCharacterParameterComponent()
    end))
    if not valid(component) then
        return false, "missing_parameter_component"
    end

    local ok, error_message = pcall(function()
        -- Native game path. Do not replace with FullRecoveryHP or SetHP.
        component:ReviveFromDying()
    end)
    if not ok then
        return false, "native_revive_failed:" .. tostring(error_message)
    end

    log("revived one Pal via " .. event_name)
    return true, "revived"
end

local function find_holder(player)
    if not valid(player) then
        return nil
    end

    local all_holders = FindAllOf("PalOtomoHolderComponentBase")
    if all_holders == nil then
        return nil
    end

    for _, holder_value in pairs(all_holders) do
        local holder = unwrap(holder_value)
        if valid(holder) then
            local owner_character = unwrap(safe_call(function()
                return holder:TryGetOwnerControlledCharacter()
            end))
            if not valid(owner_character) then
                owner_character = unwrap(safe_call(function()
                    return holder:GetOwner()
                end))
            end

            if same_object(owner_character, player) then
                return holder
            end
        end
    end

    return nil
end

local function get_party_handles(holder)
    if not valid(holder) then
        return nil
    end

    local handles = unwrap(safe_call(function()
        return holder:GetAllIndividualHandle()
    end))
    if handles ~= nil then
        return handles
    end

    local container = unwrap(field(holder, "CharacterContainer"))
    if not valid(container) then
        container = unwrap(safe_call(function()
            return holder:TryGetContainer()
        end))
    end
    if not valid(container) then
        return nil
    end

    return safe_call(function()
        return container:GetSlots()
    end)
end

local function process_party(player, event_name)
    local holder = find_holder(player)
    if not valid(holder) then
        log("party holder not found for " .. event_name)
        return
    end

    local handles = get_party_handles(holder)
    if handles == nil then
        log("party handles not available for " .. event_name)
        return
    end

    local processed = 0
    local revived = 0
    local skipped = 0

    visit_array(handles, function(value)
        local handle = value
        if valid(handle) and handle.GetHandle ~= nil then
            handle = unwrap(safe_call(function() return handle:GetHandle() end))
        end

        if valid(handle) then
            processed = processed + 1
            local did_revive, reason = revive_if_incapacitated(handle, event_name)
            if did_revive then
                revived = revived + 1
            elseif LOG_SKIPPED then
                debug("skipped party Pal (" .. tostring(reason) .. ")")
                skipped = skipped + 1
            end
        end
    end)

    log(string.format("%s party scan complete: handles=%d revived=%d skipped=%d", event_name, processed, revived, skipped))
end

local function player_state(player)
    local state = unwrap(safe_call(function()
        return player:GetCachedPlayerState()
    end))
    if valid(state) then
        return state
    end

    local controller = unwrap(safe_call(function()
        return player:GetPalPlayerController()
    end))
    if not valid(controller) then
        return nil
    end

    return unwrap(safe_call(function()
        return controller:GetPalPlayerState()
    end))
end

local function palbox_target_guid(player)
    local state = player_state(player)
    if not valid(state) then
        return nil
    end

    local storage = unwrap(safe_call(function()
        return state:GetPalStorage()
    end))
    if not valid(storage) then
        return nil
    end

    local target_container = unwrap(field(storage, "TargetContainer"))
    if not valid(target_container) then
        return nil
    end

    local container_id = unwrap(safe_call(function()
        return target_container:GetId()
    end))
    return guid_key(id_from_container_id(container_id))
end

local function current_handle_container_guid(handle)
    local parameter = get_parameter(handle)
    if not valid(parameter) then
        return nil
    end

    local save_parameter = unwrap(safe_call(function()
        return parameter:GetSaveParameter()
    end))
    if save_parameter == nil then
        return nil
    end

    local slot_id = unwrap(safe_call(function()
        return save_parameter.SlotId
    end))
    return guid_key(container_id_from_slot_id(slot_id))
end

local function consume_pending_box_move(holder, slot)
    local slot_index = safe_call(function()
        return slot:GetSlotIndex()
    end)
    local holder_container = unwrap(field(holder, "CharacterContainer"))
    local holder_container_id = nil
    if valid(holder_container) then
        local id = unwrap(safe_call(function() return holder_container:GetId() end))
        holder_container_id = guid_key(id_from_container_id(id))
    end

    local current_time = now_seconds()
    for index = #pending_box_moves, 1, -1 do
        local pending = pending_box_moves[index]
        if pending.expires_at < current_time then
            table.remove(pending_box_moves, index)
        elseif pending.slot_index == slot_index
            and (pending.source_container == nil or pending.source_container == holder_container_id) then
            table.remove(pending_box_moves, index)
            return pending
        end
    end

    return nil
end

local function on_box_move_request_pre(context_param, slot_id_param, target_container_param)
    local slot_id = unwrap(slot_id_param)
    local target_container = unwrap(target_container_param)
    local target_guid = guid_key(id_from_container_id(target_container))
    local source_guid = guid_key(container_id_from_slot_id(slot_id))
    local slot_index = slot_index_from_slot_id(slot_id)

    if target_guid == nil or slot_index == nil then
        return
    end

    pending_box_moves[#pending_box_moves + 1] = {
        target_container = target_guid,
        source_container = source_guid,
        slot_index = slot_index,
        expires_at = now_seconds() + 3.0,
    }
    debug("Palbox move candidate recorded")
end

local function on_party_slot_updated(context_param, slot_param, last_handle_param)
    local holder = unwrap(context_param)
    local slot = unwrap(slot_param)
    local last_handle = unwrap(last_handle_param)
    if not valid(holder) or not valid(slot) or not valid(last_handle) then
        return
    end

    local pending = consume_pending_box_move(holder, slot)
    if pending == nil then
        return
    end

    schedule(function()
        local player = unwrap(safe_call(function()
            return holder:TryGetOwnerControlledCharacter()
        end))
        if not valid(player) then
            return
        end

        -- The pending request came from RequestMoveToPalBox and this exact
        -- active-party slot changed. The LastHandle is the one moved Pal.
        local current_guid = current_handle_container_guid(last_handle)
        if current_guid ~= nil and current_guid ~= pending.target_container then
            debug("Palbox move candidate did not settle in the requested container")
            return
        end

        revive_if_incapacitated(last_handle, "PalboxMove")
    end)
end

local function on_respawn(context_param)
    local player = unwrap(context_param)
    if not valid(player) then
        return
    end

    schedule(function()
        process_party(player, "RespawnComplete")
    end)
end

local function same_guild(player, base_camp)
    local state = player_state(player)
    if not valid(state) or not valid(base_camp) then
        return false
    end

    local guild = unwrap(field(state, "GuildBelongTo"))
    if not valid(guild) then
        return false
    end

    local guild_id = guid_key(unwrap(safe_call(function() return guild:GetId() end)))
    local base_guild_id = guid_key(unwrap(safe_call(function()
        return base_camp:GetGroupIdBelongTo()
    end)))

    return guild_id ~= nil and guild_id == base_guild_id
end

local function on_enter_base_camp(context_param, base_camp_param)
    local builder = unwrap(context_param)
    local base_camp = unwrap(base_camp_param)
    if not valid(builder) or not valid(base_camp) then
        return
    end

    local player = unwrap(safe_call(function()
        return builder:GetOwner()
    end))
    if not valid(player) then
        return
    end

    if not same_guild(player, base_camp) then
        debug("ignored entry into another guild's base")
        return
    end

    schedule(function()
        process_party(player, "OwnGuildBaseEntry")
    end)
end

local function register_post_hook(path, callback)
    local ok, error_message = pcall(function()
        RegisterHook(path, function() end, callback)
    end)
    hook_status[path] = ok
    if ok then
        log("hook registered: " .. path)
    else
        log("hook registration failed: " .. path .. " (" .. tostring(error_message) .. ")")
    end
end

local function register_pre_post_hook(path, pre_callback, post_callback)
    local ok, error_message = pcall(function()
        RegisterHook(path, pre_callback, post_callback)
    end)
    hook_status[path] = ok
    if ok then
        log("hook registered: " .. path)
    else
        log("hook registration failed: " .. path .. " (" .. tostring(error_message) .. ")")
    end
end

log("main.lua loaded; server-side only; native revive path enabled")

local function register_hooks()
    log("registering hooks after delayed UE initialization")

    register_pre_post_hook(
        "/Script/Pal.PalNetworkCharacterContainerComponent:RequestMoveToPalBox_ToServer_Rep",
        on_box_move_request_pre,
        function() end
    )

    register_post_hook(
        "/Script/Pal.PalOtomoHolderComponentBase:OnUpdateSlot",
        on_party_slot_updated
    )

    register_post_hook(
        "/Script/Pal.PalPlayerCharacter:CallRespawnDelegate",
        on_respawn
    )

    register_post_hook(
        "/Script/Pal.PalBuilderComponent:OnEnterBaseCamp",
        on_enter_base_camp
    )
end

if ExecuteWithDelay ~= nil then
    ExecuteWithDelay(10000, register_hooks)
else
    log("ExecuteWithDelay is unavailable; hooks were not registered")
end
