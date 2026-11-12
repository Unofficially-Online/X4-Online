-- Opt-in Linux keyboard replay. Canonical mode keeps the original single-ship
-- diagnostic; independent mode requires the separate disposable physics owner.
X4LAN_LINUX_KEYBOARD = {}

function X4LAN_LINUX_KEYBOARD.create(gate, config, register_section, ffi, log)
    local settings = config.native_gate_stock_keyboard_replay_probe
    assert(type(settings) == 'table', 'Linux keyboard probe settings missing')
    assert(config.native_gate_stock_keyboard_observer == true and gate.platform() == 'linux',
        'Linux keyboard replay requires the native observer')
    assert(config.native_gate_clients == 2, 'Linux keyboard replay requires two clients')
    assert(settings.independent_players == nil or type(settings.independent_players) == 'boolean',
        'independent_players must be boolean')
    local independent = settings.independent_players == true
    local camera_setting = config.native_gate_linux_external_camera_probe
    assert(camera_setting == nil or type(camera_setting) == 'boolean',
        'external camera probe must be boolean')
    local external_camera = camera_setting == true
    local presentation_setting = config.native_gate_linux_presentation_probe
    assert(presentation_setting == nil or type(presentation_setting) == 'boolean',
        'presentation probe must be boolean')
    local presentation = presentation_setting == true
    local fork_setting = config.native_gate_linux_presentation_fork_probe
    assert(fork_setting == nil or type(fork_setting) == 'boolean', 'presentation fork probe must be boolean')
    local presentation_fork = fork_setting == true
    assert(not presentation_fork or (independent and presentation and not config.native_gate_fork_save and
        type(gate.player_presentation_fork_arm) == 'function' and
        type(gate.player_presentation_fork_result) == 'function'),
        'presentation fork requires independent presentation, native methods and no fork-save')
    local overlay_setting = config.native_gate_linux_ui_overlay_probe
    assert(overlay_setting == nil or type(overlay_setting) == 'boolean', 'UI overlay probe must be boolean')
    local ui_overlay = overlay_setting == true
    local overlay_callbacks = config.native_gate_linux_ui_overlay_callbacks
    if overlay_callbacks == nil then overlay_callbacks = 1 end
    assert(type(overlay_callbacks) == 'number' and (overlay_callbacks == 1 or overlay_callbacks == 8 or overlay_callbacks == 160) and
        (overlay_callbacks == 1 or ui_overlay), 'UI overlay callbacks require 1 or explicit enabled 8/160')
    local correction_setting = config.native_gate_linux_ui_seat_correction
    assert(correction_setting == nil or type(correction_setting) == 'boolean',
        'seat correction must be boolean')
    assert(correction_setting ~= true or (ui_overlay and overlay_callbacks == 160 and
        config.native_gate_linux_ui_seat_query_probe == true and
        config.native_gate_linux_ui_live_pose_observer == true),
        'seat correction requires explicit live observer, seat query and UI overlay160')
    local overlay_result_tick = 9 + overlay_callbacks
    local view_query_setting = config.native_gate_linux_view_query_observer
    assert(view_query_setting == nil or type(view_query_setting) == 'boolean',
        'view query observer must be boolean')
    local view_queries = view_query_setting == true
    local ui_callback_setting = config.native_gate_linux_ui_callback_observer
    assert(ui_callback_setting == nil or type(ui_callback_setting) == 'boolean',
        'UI callback observer must be boolean')
    local ui_callbacks = ui_callback_setting == true
    assert(not ui_callbacks or (view_queries and presentation and
        type(gate.player_ui_callback_close) == 'function' and
        type(gate.player_ui_callback_result) == 'function'),
        'UI callback observer requires view query and presentation probes')
    assert(not ui_overlay or (presentation_fork and view_queries and ui_callbacks and
        type(gate.player_ui_overlay_arm) == 'function' and
        type(gate.player_ui_overlay_result) == 'function'),
        'UI overlay requires fork, view query, callbacks and native methods')
    assert(not view_queries or (independent and
        type(gate.player_view_query_observe) == 'function' and
        type(gate.player_view_query_phase) == 'function' and
        type(gate.player_view_query_finish) == 'function'),
        'view query observer requires independent controls and native methods')
    assert(not presentation or (independent and type(gate.player_presentation_probe) == 'function'),
        'presentation probe requires independent controls and its native method')
    assert(not external_camera or (independent and
        type(gate.player_external_camera_probe) == 'function'),
        'external camera probe requires independent controls and its native method')
    assert(not config.native_gate_pilot_inputs and not config.native_gate_player_probe,
        'Linux keyboard replay excludes other player controllers')

    local slot = assert(tonumber(config.slot:match('^p([12])$')), 'invalid keyboard slot')
    local lookahead = config.native_gate_lookahead or 0
    -- The bundle reserves two final ticks to drain this diagnostic's records.
    assert(lookahead == 0, 'Linux keyboard probe currently requires zero lookahead')
    local delay = lookahead + 2
    local last_capture = settings.last_capture
    assert(type(last_capture) == 'number' and last_capture % 1 == 0 and last_capture >= 3,
        'invalid keyboard capture window')
    assert(not external_camera or last_capture >= 5,
        'external camera probe requires a window through tick seven')
    assert(not presentation or last_capture >= 6,
        'presentation probe requires a window through tick eight')
    assert(not view_queries or last_capture >= 6,
        'view query observer requires a window through tick eight')
    assert(not presentation_fork or last_capture >= 7, 'presentation fork requires at least five independent frames')
    assert(not ui_overlay or last_capture >= overlay_result_tick - 2, 'UI overlay result exceeds independent frame window')
    local neutral_ship = settings.neutral_ship
    assert(type(neutral_ship) == 'string' and #neutral_ship == 16 and neutral_ship:match('^[0-9a-f]+$')
        and neutral_ship ~= '0000000000000000', 'neutral fixture ship missing')

    local started = false
    local canonical_runtime
    local canonical_saved
    local camera_attempted = false
    local presentation_attempted = false
    local fork_armed, fork_finished = false, false
    local overlay_armed, overlay_finished = false, false
    local view_query_attempted = false
    local view_query_finished = false
    local ui_callback_closed = false
    local probe = {}

    local function status()
        return tostring(gate.stock_keyboard_replay_status())
    end

    function probe.frame(tick)
        if not started then
            assert(tick == 1, 'keyboard replay must start at tick one')
            canonical_runtime = ffi.C.GetPlayerOccupiedShipID()
            canonical_saved = gate.saved_identity((tostring(canonical_runtime):gsub('ULL$', '')))
            assert(canonical_saved and canonical_saved ~= neutral_ship, 'canonical/neutral identity collision')
            local accepted
            if independent then
                accepted = gate.stock_keyboard_replay_request(slot, last_capture, delay, lookahead,
                    canonical_saved, neutral_ship, 1)
            else
                accepted = gate.stock_keyboard_replay_request(slot, last_capture, delay, lookahead,
                    canonical_saved, neutral_ship)
            end
            assert(accepted == 1, 'keyboard request refused: '..status())
            local owner = independent and 'linux_independent_keyboard' or 'linux_canonical_keyboard'
            register_section('stock', owner, {
                apply = function(_, records)
                    local count = 0
                    for source_slot = 1, 2 do
                        local payload = records[source_slot]
                        if payload and payload ~= '' then
                            assert(gate.stock_keyboard_replay_stage(source_slot, payload) == 1,
                                'keyboard stage refused: '..status())
                            count = count + 1
                        end
                    end
                    return 'linux_keyboard='..count
                end,
                proposal = function(capture_tick)
                    if capture_tick < 3 or capture_tick > last_capture then
                        return nil
                    end
                    local record = assert(gate.stock_keyboard_replay_capture(),
                        'keyboard capture refused: '..status())
                    log('LINUX_KEYBOARD_CAPTURE tick='..capture_tick..' record='..record)
                    return record
                end,
            })
            started = true
            local target = independent and (' p2='..neutral_ship..' mode=independent') or (' neutral='..neutral_ship)
            log('LINUX_KEYBOARD_ARMED slot='..slot..' canonical='..canonical_saved..
                target..' first=3 last='..last_capture..' delay='..delay)
        end
        assert(ffi.C.GetPlayerOccupiedShipID() == canonical_runtime,
            'canonical keyboard ship changed')
        assert(ffi.C.IsComponentOperational(canonical_runtime), 'canonical keyboard ship unavailable')
        local position = ffi.C.GetObjectPositionInSector(canonical_runtime)
        for _, field in ipairs({'x', 'y', 'z', 'yaw', 'pitch', 'roll'}) do
            local value = position[field]
            assert(type(value) == 'number' and value == value and math.abs(value) ~= math.huge,
                'nonfinite canonical keyboard pose: '..field)
        end
        local pose = string.format('%s/%.17g/%.17g/%.17g/%.17g/%.17g/%.17g', canonical_saved,
            position.x, position.y, position.z, position.yaw, position.pitch, position.roll)
        -- This disposable experiment changes only client 2's view. The
        -- canonical simulation player stays P1, as it does in the input path.
        -- The native method authenticates its phase and retained target again.
        if external_camera and slot == 2 and tick == 6 then
            assert(not camera_attempted, 'external camera probe cannot be retried')
            camera_attempted = true
            assert(gate.player_external_camera_probe() == 1,
                'external camera probe refused; see native camera evidence')
            log('LINUX_EXTERNAL_CAMERA tick=6 slot=2 target='..neutral_ship)
        end
        if view_queries and tick == 7 then
            assert(not view_query_attempted, 'view query observer cannot be retried')
            view_query_attempted = true
            assert(gate.player_view_query_observe() == 1,
                'view query observer refused; see native evidence')
            log('LINUX_VIEW_QUERY_ARMED tick=7 slot='..slot)
        end
        if presentation and tick == 7 then
            assert(not presentation_attempted, 'presentation probe cannot be retried')
            presentation_attempted = true
            assert(gate.player_presentation_probe() == 1,
                'presentation probe refused; see native presentation evidence')
            log('LINUX_PRESENTATION_PROBE tick=7 slot='..slot)
        end
        if presentation_fork and tick == 7 then
            assert(presentation_attempted and not fork_armed, 'presentation fork arm out of order')
            assert(gate.player_presentation_fork_arm() == 1, 'presentation fork arm refused')
            fork_armed = true
            log('LINUX_PRESENTATION_FORK_ARMED tick=7 slot='..slot)
        end
        if presentation_fork and tick == 8 then
            assert(fork_armed and not fork_finished, 'presentation fork result out of order')
            local receipt = assert(gate.player_presentation_fork_result(), 'presentation fork result refused')
            fork_finished = true
            log('LINUX_PRESENTATION_FORK_RESULT tick=8 slot='..slot..' '..receipt)
            if ui_overlay and slot == 2 then
                assert(not overlay_armed and gate.player_ui_overlay_arm() == 1, 'UI overlay arm refused')
                overlay_armed = true
                log('LINUX_UI_OVERLAY_ARMED tick=8 slot=2')
            end
        end
        if ui_overlay and slot == 2 and tick == overlay_result_tick then
            assert(overlay_armed and not overlay_finished, 'UI overlay result out of order')
            local receipt = assert(gate.player_ui_overlay_result(), 'UI overlay result refused')
            overlay_finished = true
            log('LINUX_UI_OVERLAY_RESULT tick='..tick..' slot=2 '..receipt)
        end
        if ui_callbacks and tick == last_capture + delay - 1 then
            assert(view_query_attempted and not ui_callback_closed, 'UI callback close out of order')
            assert(gate.player_ui_callback_close() == 1, 'UI callback close refused')
            ui_callback_closed = true
            log('LINUX_UI_CALLBACK_CLOSE tick='..tick..' slot='..slot)
        end
        if ui_callbacks and tick == last_capture + delay then
            assert(ui_callback_closed, 'UI callback result before close')
            log('LINUX_UI_CALLBACK_RESULT tick='..tick..' slot='..slot..' '..
                assert(gate.player_ui_callback_result(), 'UI callback result unavailable'))
        end
        if view_queries and tick == last_capture + delay then
            assert(view_query_attempted and not view_query_finished,
                'view query observer completion out of order')
            view_query_finished = true
            log('LINUX_VIEW_QUERY_RESULT tick='..tick..' slot='..slot..' '..
                assert(gate.player_view_query_finish(), 'view query observer report unavailable'))
        end
        log('LINUX_KEYBOARD_SHIP tick='..tick..' pose='..pose)
        log('LINUX_KEYBOARD_STATUS tick='..tick..' '..status())
        return 'LINUX_KEYBOARD_SHIP_1:'..pose
    end

    function probe.stop()
        log('LINUX_KEYBOARD_STOP '..status())
    end

    return probe
end
