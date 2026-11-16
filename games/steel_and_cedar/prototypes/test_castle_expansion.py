#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check the playable layout's routes and room separation, independent of rendering."""
from collections import deque
import unittest
from castle_expansion import create_project, WIDTH, HEIGHT, ROOMS


class CastleLayout(unittest.TestCase):
    def setUp(self):
        self.project = create_project()
        self.map = self.project['maps'][0]
        self.tiles = self.map['terrain']
        self.battle = self.project['campaigns'][0]['flow']['nodes'][0]

    def test_all_rooms_have_reachable_floor_and_a_separate_reveal(self):
        hidden = {(c['x'], c['y']) for c in self.map['visualVisibility']['hiddenCells']}
        seen, queue = {(8, 5)}, deque([(8, 5)])
        while queue:
            x, y = queue.popleft()
            for nx, ny in [(x-1,y),(x+1,y),(x,y-1),(x,y+1)]:
                if not (0 <= nx < WIDTH and 0 <= ny < HEIGHT): continue
                if self.tiles[ny*WIDTH+nx].startswith('mountain'): continue
                if (nx,ny) not in seen: seen.add((nx,ny)); queue.append((nx,ny))
        self.assertTrue(hidden <= seen, 'every covered cell must be accessible through a real route')
        masks = [{(x,y) for y in range(top,top+h) for x in range(left,left+w)}
                 for left,top,w,h in ROOMS.values()]
        self.assertEqual(set.union(*masks), hidden)
        radius = self.map['visualVisibility']['revealRadius']
        for i, room in enumerate(masks):
            for other in masks[i+1:]:
                self.assertGreater(min(abs(x-a)+abs(y-b) for x,y in room for a,b in other), radius,
                                   'entering one room must not reveal another through a wall')
        self.assertNotIn((18,15), hidden, 'the open court is never roofed')

    def test_each_player_mode_has_legal_distinct_starts_and_a_reachable_boss(self):
        for players in [1,2]:
            occupied = set()
            for unit in self.battle['placements']:
                if unit.get('minimumPlayers',1) > players: continue
                point = unit.get('twoPlayerPosition',{}) if players == 2 else {}
                x,y = point.get('x',unit['x']), point.get('y',unit['y'])
                self.assertTrue(0 <= x < WIDTH and 0 <= y < HEIGHT)
                self.assertFalse(self.tiles[y*WIDTH+x].startswith('mountain'))
                if not unit.get('arrival'):
                    self.assertNotIn((x,y),occupied)
                    occupied.add((x,y))
            forces = [u for u in self.battle['placements'] if u['side']=='first' and u.get('minimumPlayers',1)<=players]
            self.assertEqual(len(forces),2*players)
        boss = next(u for u in self.battle['placements'] if u['id']=='lord_masatora')
        self.assertEqual(self.tiles[boss['y']*WIDTH+boss['x']],'paved_castle_dais')

    def test_prototype_does_not_reuse_the_demo_save_namespace(self):
        self.assertEqual(self.map['width']*self.map['height'],768)
        self.assertEqual(self.project['gameId'],'grandleon.castle_expansion_study')
        self.assertNotEqual(self.project['packageId'],'751d322d-dc60-4c6d-a1e1-821e4dd7c617')
        self.assertEqual(self.project['campaigns'][0]['playerMode'],'choice')

if __name__ == '__main__': unittest.main()
