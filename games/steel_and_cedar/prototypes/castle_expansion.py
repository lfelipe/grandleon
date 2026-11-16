#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build a playable larger-castle study without replacing the demo campaign."""
import argparse
import copy
import json
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / 'source/project.json'
WIDTH, HEIGHT = 32, 24
ROOMS = {
    'guardhouse': (10, 3, 7, 6),
    'council': (21, 3, 8, 6),
    'great_hall': (21, 11, 8, 10),
}


def create_project():
    project = json.loads(SOURCE.read_text())
    cells = [['grass'] * WIDTH for _ in range(HEIGHT)]
    def area(x, y, w, h, terrain):
        for row in range(y, y + h):
            for col in range(x, x + w): cells[row][col] = terrain
    # A hilltop fortress, not isolated buildings scattered across a field.
    area(0, 0, WIDTH, 2, 'hills')
    area(0, 22, WIDTH, 2, 'hills')
    area(30, 0, 2, HEIGHT, 'hills')
    area(0, 2, 3, 20, 'forest')
    area(3, 5, 6, 2, 'road')
    area(4, 6, 2, 13, 'road')
    area(4, 16, 5, 2, 'road')
    area(9, 2, 21, 20, 'mountain_castle_wall')
    area(10, 3, 19, 18, 'paved')
    # West guardhouse, a central open-air gallery/court, and two eastern rooms.
    area(17, 2, 1, 8, 'mountain_castle_wall')
    area(9, 9, 9, 1, 'mountain_castle_wall')
    area(20, 2, 1, 20, 'mountain_castle_wall')
    area(20, 9, 10, 2, 'mountain_castle_wall')
    for x, y, w, h in ROOMS.values(): area(x, y, w, h, 'paved_castle_floor')
    for x, y in [(9, 5), (17, 5), (20, 5), (9, 16), (20, 15)]:
        area(x, y, 1, 2, 'paved_castle_door')
    area(27, 15, 2, 2, 'paved_castle_dais')
    # A garden beside the southern approach and a service bay in the court.
    area(6, 11, 2, 2, 'mountain_walled_garden')
    area(12, 17, 2, 2, 'mountain_abandoned_cart')
    area(11, 12, 2, 2, 'mountain_village_well')
    hidden = [{'x': x, 'y': y} for y in range(HEIGHT) for x in range(WIDTH)
              if cells[y][x] in ['paved_castle_floor', 'paved_castle_dais']]
    project.update(packageId='f5eef201-a8f3-4eec-a22a-665807c75006',
                   gameId='grandleon.castle_expansion_study',
                   title='The Three Courts — castle prototype', contentRevision='0.1.0')
    project['maps'] = [{'id': 'three_courts', 'name': 'The Three Courts',
                        'width': WIDTH, 'height': HEIGHT,
                        'terrain': [tile for row in cells for tile in row],
                        'surroundings': {'mode': 'continue', 'depth': 5},
                        'visualVisibility': {'revealRadius': 2, 'hiddenCells': hidden}}]
    campaign = project['campaigns'][0]
    encounter = copy.deepcopy(next(n for n in campaign['flow']['nodes'] if n.get('mapId') == 'twin_gate'))
    encounter.update(id='three_courts', name='The Three Courts', mapId='three_courts')
    positions = {
        'rowan_gate': (8, 5), 'tomas_gate': (7, 6),
        'akari_gate': (8, 16), 'ren_gate': (7, 17),
        'lord_masatora': (27, 15), 'gate_north_guard': (14, 6),
        'gate_south_guard': (15, 15), 'gate_center_mystic': (25, 6),
        'gate_west_knife': (18, 12), 'gate_wave_high': (10, 6),
        'gate_wave_low': (10, 17),
    }
    for unit in encounter['placements']:
        unit['x'], unit['y'] = positions[unit['id']]
        unit.pop('twoPlayerPosition', None)
        # Close solo starts; co-op opens two separated routes into the court.
        if unit['id'] == 'rowan_gate': unit['twoPlayerPosition'] = {'x': 6, 'y': 5}
        if unit['id'] == 'tomas_gate': unit['twoPlayerPosition'] = {'x': 6, 'y': 6}
        if unit['side'] == 'second': unit['behavior'] = 'hold'
    campaign.update(id='three_courts', name='The Three Courts')
    endings = [copy.deepcopy(n) for n in campaign['flow']['nodes'] if n['id'] in ['epilogue', 'campaign_defeat', 'campaign_complete', 'campaign_failed']]
    campaign['flow'].update(entryNodeId='three_courts', nodes=[encounter] + endings)
    # Keep the playable prototype's orders about its actual routes and rooms.
    lines = {
        'gate_orders': 'The western guardhouse leads to the open court. The council chamber lies to the north; Masatora holds the great hall to the south. Rooms uncover as we enter and explore them.',
        'gate_coop': 'Rowan takes the guardhouse gate. Akari enters through the southern court. Choose whether to clear the council room or converge on the great hall; neither army should advance alone.',
    }
    for dialogue in project['dialogues']:
        if dialogue['id'] in lines:
            for index, line in enumerate(dialogue['lines']):
                line['text'] = lines[dialogue['id']] if index == 0 else 'Keep the connecting passages clear; we can regroup in the open court.'
    # Old translations would describe the old battlefield; prototype is English.
    project.pop('translations', None)
    project['notes'] = ('Playable 32×24 layout study, not a tuned replacement campaign. '
                        'Three covered rooms, a visible gallery/courtyard, two co-op approaches and a final hall. '
                        'Console capacity and modular castle rendering must be extended before console export.')
    return project


def render_plan(project):
    """An overhead reading aid drawn from the same authored cells and starts."""
    from html import escape
    tile, margin = 22, 26
    width, height = WIDTH * tile + margin * 2, HEIGHT * tile + 100
    colours = {'grass': '#92aa78', 'forest': '#416449', 'hills': '#738367',
               'road': '#b89a6c', 'paved': '#b3afa0',
               'mountain_castle_wall': '#4d5353', 'paved_castle_floor': '#dbd3b8',
               'paved_castle_dais': '#af7864', 'paved_castle_door': '#edbe60'}
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
             '<rect width="100%" height="100%" fill="#101c23"/>',
             '<text x="26" y="24" fill="#f4ead2" font-family="sans-serif" font-size="17">The Three Courts · 32 × 24 · co-op plan</text>']
    for index, kind in enumerate(project['maps'][0]['terrain']):
        x, y = index % WIDTH, index // WIDTH
        colour = colours.get(kind, '#77866a')
        parts.append(f'<rect x="{margin+x*tile}" y="{38+y*tile}" width="{tile}" height="{tile}" fill="{colour}" stroke="#314039" stroke-width=".4"/>')
    for name,(x,y,w,h) in ROOMS.items():
        label = name.replace('_', ' ').title()
        cx,cy = margin+(x+w/2)*tile,38+(y+h/2)*tile
        parts.extend([f'<rect x="{cx-56}" y="{cy-11}" width="112" height="22" rx="4" fill="#15202c" opacity=".88"/>',
                      f'<text x="{cx}" y="{cy+4}" text-anchor="middle" fill="white" font-family="sans-serif" font-size="12">{escape(label)}</text>'])
    for unit in project['campaigns'][0]['flow']['nodes'][0]['placements']:
        point = unit.get('twoPlayerPosition', unit)
        x,y = margin+(point['x']+.5)*tile,38+(point['y']+.5)*tile
        colour = '#f18b82' if unit['side']=='second' else '#6bceef' if unit.get('player',1)==1 else '#e9cb69'
        parts.append(f'<circle cx="{x}" cy="{y}" r="6" stroke="#102030" stroke-width="1.5" fill="{colour if not unit.get("arrival") else "none"}"><title>{escape(unit.get("name",unit["id"]))}</title></circle>')
    parts.append(f'<text x="26" y="{height-22}" fill="#d4ddd8" font-family="sans-serif" font-size="12">Blue: Rowan · Gold: Akari · Coral: enemies · Hollow: arrivals · Amber tiles: doors</text></svg>')
    return ''.join(parts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('out/castle-expansion06/project.json'))
    parser.add_argument('--plan', type=Path, help='Also draw an overhead map plan')
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    project = create_project()
    args.output.write_text(json.dumps(project, ensure_ascii=False, indent=2) + '\n')
    if args.plan:
        args.plan.parent.mkdir(parents=True, exist_ok=True)
        args.plan.write_text(render_plan(project))
    print(args.output)

if __name__ == '__main__': main()
