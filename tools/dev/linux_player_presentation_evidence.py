"""Strict read-only presentation probe evidence for the disposable Linux run."""
import re

PREFIX = 'X4LAN PRESENTATION_PROBE v=1 '
ROW = re.compile(r'stage=(p1|p2) tick=7 slot=([12]) player=([0-9a-f]{1,16}) parent=([0-9a-f]{1,16}) '
    r'vector=([0-9a-f]{1,16}),([0-9a-f]{1,16}),([0-9a-f]{1,16}) position=([0-9]{1,20}) bank=([01]) '
    r'cache_parent=([0-9a-f]{1,16}) live_called=([01]) live=([0-9a-f]{1,16}) '
    r'cached_called=([01]) cached=([0-9a-f]{1,16}) omitted=([01]) '
    r'refs=([0-9]{1,20}),([0-9]{1,20}),([0-9]{1,20}) marker=([0-9]{1,20}),([0-9]{1,20})\n')
UI_PLAN = re.compile(r'stage=ui_plan tick=7 slot=([12]) manager=([0-9a-f]{1,16}) presenter=([0-9a-f]{1,16}) '
    r'presenter_vtable=([0-9a-f]{1,16}) controller=([0-9a-f]{1,16}) '
    r'controller_vtable=([0-9a-f]{1,16}) flag=([01]) authenticated=1\n')
OCCUPIED = re.compile(r'stage=occupied tick=7 slot=([12]) runtime=([0-9]{1,20}) observational=1\n')
RESULT = re.compile(r'stage=result tick=7 slot=([12]) success=1 reason=ok\n')


def validate_presentation_rows(lines, slot):
    """Validate exact rows before they can be removed from the frame stream."""
    errors = []
    matches = [(index, line) for index, line in enumerate(lines) if 'X4LAN PRESENTATION_PROBE' in line]
    if slot not in (1, 2) or len(matches) != 5:
        return {'pass': False, 'errors': ['presentation requires exactly five rows'], 'indices': []}
    indices = [item[0] for item in matches]
    if indices != list(range(indices[0], indices[0] + 5)):
        errors.append('presentation rows are not contiguous')
    if any(not line.startswith(PREFIX) for _, line in matches):
        return {'pass': False, 'errors': errors + ['malformed presentation prefix'], 'indices': []}
    first, second = (ROW.fullmatch(matches[index][1][len(PREFIX):]) for index in (0, 1))
    ui_plan = UI_PLAN.fullmatch(matches[2][1][len(PREFIX):])
    occupied = OCCUPIED.fullmatch(matches[3][1][len(PREFIX):])
    result = RESULT.fullmatch(matches[4][1][len(PREFIX):])
    if not first or not second or not ui_plan or not occupied or not result:
        return {'pass': False, 'errors': errors + ['malformed presentation rows'], 'indices': []}
    if first[1] != 'p1' or second[1] != 'p2' or any(int(value) != slot for value in (first[2], second[2], ui_plan[1], occupied[1], result[1])):
        errors.append('presentation stages or slot mismatch')
    observations = []
    for row in (first, second):
        player, parent, begin, end, capacity = (int(row[index], 16) for index in range(3, 8))
        position, bank = int(row[8]), int(row[9])
        cache_parent, live = int(row[10], 16), int(row[12], 16)
        cached = int(row[14], 16)
        live_called, cached_called, omitted = int(row[11]), int(row[13]), int(row[15])
        refs = tuple(int(row[index]) for index in (16, 17, 18))
        marker = tuple(int(row[index]) for index in (19, 20))
        if (not player or max(player, parent, begin, end, capacity, cache_parent, live, cached) > 0xffffffffffffffff or
                position > 0xffffffff or any(value > 0xffffffff for value in refs + marker)):
            errors.append('presentation field outside native range')
        if (not live_called and (live or cached_called or cached or not omitted)) or (
                live_called and (not parent or live != parent)) or (
                cached_called and (not live_called or omitted or cached != parent or cache_parent != parent or
                                   not 0 < refs[0] < 0xffffffff or refs[1] != refs[0] + 1 or
                                   refs[2] != refs[0] or marker != (2, 2))) or (
                not cached_called and (cached or not omitted or refs != (0, 0, 0) or marker != (0, 0))) or (
                live_called and not cached_called and cache_parent == parent):
            errors.append('presentation getter or omission invariant failed')
        observations.append({'player': player, 'parent': parent, 'bank': bank,
                             'live_called': live_called, 'cached_called': cached_called, 'omitted': omitted})
    if observations[0]['player'] == observations[1]['player'] or observations[0]['bank'] != observations[1]['bank']:
        errors.append('presentation player identity or bank mismatch')
    manager, presenter, presenter_vtable, controller, controller_vtable = (
        int(ui_plan[index], 16) for index in range(2, 7))
    if (any(pointer < 0x10000 or pointer > 0x00007fffffffffff or pointer % 8
            for pointer in (manager, presenter, controller)) or
            presenter_vtable != 0x3ab4600 or controller_vtable != 0x3ab4060):
        errors.append('presentation UI plan identity mismatch')
    if int(occupied[2]) > 0xffffffffffffffff:
        errors.append('presentation occupied runtime invalid')
    return {'pass': not errors, 'errors': errors, 'indices': indices if not errors else [],
            'observations': observations, 'occupied_runtime': int(occupied[2]),
            'ui_plan': {'manager': manager, 'presenter': presenter, 'controller': controller,
                        'presenter_vtable': presenter_vtable, 'controller_vtable': controller_vtable,
                        'first_person': int(ui_plan[7]), 'authenticated': True}}
