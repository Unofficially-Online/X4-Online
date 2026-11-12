"""Explicit finite disposable-session budgets, separate from legacy probes."""
MIN_FRAMES = 180
MAX_FRAMES = 900

from linux_player_startup_phases import startup_phases, phases_from_options


def session_budget(frames, startup=None):
    if type(frames) is not int or not MIN_FRAMES <= frames <= MAX_FRAMES:
        raise ValueError('session frames must be an integer from 180 through 900')
    phase = startup_phases(startup)
    end = phase['apply'] + frames - 1
    last = phase['presentation_first'] + frames - 11
    return {
        'frames': frames,
        'startup_phases': startup,
        'first': phase['presentation_first'],
        'apply': phase['apply'],
        'take': phase['take'],
        'capture': phase['capture'],
        'last_capture': end - 2,
        'recurring_end': end,
        'callbacks': frames - 10,
        'last': last,
        'report_tick': last + 1,
        'disk_limit': (512 << 20) + frames * (256 << 10),
    }


def budget_from_options(options):
    frames = options.get('player_ui_session_frames', 0)
    if type(frames) is not int:
        raise ValueError('player_ui_session_frames must be an integer')
    phases_from_options(options)
    return session_budget(frames, options.get('player_startup_phases')) if frames else None


def budget_from_overlay(overlay):
    """Only a strictly accepted session overlay proof can extend old budgets."""
    frames = overlay.get('session_frames', 0)
    budget = session_budget(frames, overlay.get('startup_phases')) if frames else {'callbacks': 160, 'last': 168}
    enabled_mismatch = (overlay.get('enabled') is not False and
                        overlay.get('callback_count') != budget['callbacks'])
    if frames and (overlay.get('pass') is not True or enabled_mismatch):
        raise ValueError('session overlay proof or callback budget mismatch')
    return budget


def resolve_session_options(options, *, cli=False):
    """Resolve the explicit session mode before existing dependency admission."""
    budget = budget_from_options(options)
    if budget is None:
        return dict(options)
    legacy_option = (options.get('player_ui_overlay_probe') or
                     options.get('player_ui_overlay_callbacks', 1) != 1)
    if cli and legacy_option:
        raise ValueError('session mode excludes the legacy UI overlay flags')
    required = ('player_ui_seat_query_probe', 'player_ui_live_pose_observer',
                'player_ui_seat_correction', 'player_ui_render_lease')
    if not all(options.get(key) is True for key in required):
        raise ValueError('session mode requires explicit seat query, live observer, correction and render lease')
    conflicts = ('player_external_camera_probe', 'player_throttle_probe',
                 'player_throttle_warm_probe')
    if any(options.get(key) for key in conflicts):
        raise ValueError('session mode excludes external camera and scripted throttle diagnostics')
    independent = options.get('independent_keyboard_frames', 0)
    if independent not in (0, budget['frames']) or type(independent) is not int:
        raise ValueError('session and independent physics frame budgets must match')
    resolved = dict(options)
    resolved.update(independent_keyboard_frames=budget['frames'], player_ui_overlay_probe=True,
                    player_ui_overlay_callbacks=budget['callbacks'])
    return resolved


def expected_callbacks(options):
    budget = budget_from_options(options)
    return budget['callbacks'] if budget else 160
