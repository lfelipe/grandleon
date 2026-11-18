#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Draw compact overhead plans directly from the playable campaign source."""
import json
from html import escape
from pathlib import Path

ROOT = Path(__file__).resolve().parent
COLOURS = {'forest':'#63835b', 'farmland':'#c1ab73', 'road':'#d8b78a',
           'grass':'#a9bf85', 'hills':'#8c9c70', 'water':'#6cabc1',
           'bridge':'#997047', 'paved':'#c5bfa9', 'ruins':'#999384',
           'mountain':'#7e8278', 'bamboo':'#68895d'}
NOTES = {
    'bell_orchard': ('Orchard rows and bell court', ['A compact first battle.', 'Upper and lower approaches.', 'Sixteen orchard trees.', 'Bell spans two walkable cells.']),
    'cedar_deepwood': ('Woodland, groves and brook', ['Two irregular clearings.', 'Old trees mark the bends.', 'A wooded far bank.', 'Three short crossings.']),
    'cedar_watch': ('A ruined command court', ['One low-walled court.', 'Four approach roads.', 'Six signal beacons.', 'Survive twenty rounds.']),
    'gate_village': ('A working market village', ['Staggered homes and workshops.', 'Market well and cottage garden.', 'Cart in the loading yard.', 'Tower joins the eastern wall.']),
    'twin_gate': ('A fortified seat above town', ['Connected tower and curtain wall.', 'Clear high and low approaches.', 'Loading bay off the main ward.', 'Enter the keep to reveal its room.']),
    'orchard_storehouse': ('A walled orchard yard', ['A glint opens this detour.', 'The March takes the front entrance.', 'Cedar circles the garden wall in 2P.']),
    'courier_ford': ('A crossing with two bridges', ["Accept the courier's offer.", 'Each bridge is its own approach.', 'Watch for archers on the east bank.']),
}


def main():
    project = json.loads((ROOT / 'source/project.json').read_text())
    encounters = {n['mapId']: n for n in project['campaigns'][0]['flow']['nodes'] if n.get('kind') == 'encounter'}
    for board in project['maps']:
        key, width, height = board['id'], board['width'], board['height']
        scale = min(630 / width, 420 / height)
        left, top = 32 + (650 - width * scale) / 2, 100 + (440 - height * scale) / 2
        title, notes = NOTES[key]
        parts = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1000 600" role="img" aria-label="{escape(title)}"><style>text{{font-family:system-ui,sans-serif}}.label{{font-size:14px;fill:#e9e3cf}}</style><rect width="1000" height="600" fill="#172820"/>', f'<text x="32" y="45" fill="#f2ead5" font-size="27">{escape(board["name"])}</text>', f'<text x="32" y="74" fill="#c6d0bc" font-size="15">Playable layout · {width}×{height} · source revision {project["contentRevision"]}</text>']
        consumed = set()
        glyphs = []
        for i, terrain in enumerate(board['terrain']):
            x, y = left + (i % width) * scale, top + (i // width) * scale
            base = next((k for k in COLOURS if terrain == k or terrain.startswith(k + '_')), 'grass')
            parts.append(f'<rect x="{x:.2f}" y="{y:.2f}" width="{scale+.1:.2f}" height="{scale+.1:.2f}" fill="{COLOURS[base]}"/>')
            feature = terrain[len(base)+1:] if terrain.startswith(base+'_') else ''
            if feature and i not in consumed:
                size = 2 if feature in ('village_house','ancient_grove','abandoned_cart','village_well','watchtower','walled_garden') else 1
                if size == 2:
                    consumed.update((i, i + 1, i + width, i + width + 1))
                glyph = {'village_house':'⌂','ancient_grove':'♣','orchard_tree':'♣','orchard_bell':'B','watchtower':'T','town_wall':'▪','castle_wall':'▪','abandoned_cart':'C','village_well':'O','walled_garden':'G','signal_beacon':'!','watch_ruin':'▪','castle_door':'D','castle_floor':'·','castle_dais':'★'}.get(feature, '')
                glyphs.append(f'<text x="{x+scale*size/2:.2f}" y="{y+scale*size*.68:.2f}" text-anchor="middle" fill="#313b2c" font-size="{scale*size*.62:.2f}">{glyph}</text>')
        parts.extend(glyphs)
        def marker(x, y, text, colour):
            cx, cy = left+(x+.5)*scale, top+(y+.5)*scale
            parts.append(f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{scale*.33:.2f}" fill="{colour}" stroke="#172820"/><text x="{cx:.2f}" y="{cy+4:.2f}" text-anchor="middle" fill="#172820" font-size="11" font-weight="700">{text}</text>')
        for unit in encounters[key]['placements']:
            if unit.get('memberId') == 'rowan':
                marker(unit['x'],unit['y'],'S','#a9dde7')
                coop = unit.get('twoPlayerPosition',unit)
                marker(coop['x'],coop['y'],'1','#73b9ed')
            elif unit.get('memberId') == 'akari': marker(unit['x'],unit['y'],'2','#edc573')
        parts.append(f'<text x="710" y="125" fill="#f2ead5" font-size="19">{escape(title)}</text>')
        for i, note in enumerate(notes): parts.append(f'<text class="label" x="710" y="{164+i*28}">{escape(note)}</text>')
        for i, label in enumerate(('S · Rowan, solo start', '1 · Rowan, co-op start', '2 · Akari, co-op start', 'B · Bell    T · Tower', 'C · Cart    O · Well', 'D · Keep door    ★ · Dais')): parts.append(f'<text class="label" x="710" y="{350+i*28}">{escape(label)}</text>')
        parts.append('</svg>')
        (ROOT / 'plans' / (key+'.svg')).write_text('\n'.join(parts)+'\n')


if __name__ == '__main__': main()
