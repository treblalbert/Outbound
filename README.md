# OUTBOUND

A top-down open-world extraction shooter in C++ and OpenGL, by **Albert Freeman**.

Leave the bunker, loot a world that is generated fresh for each new day, and get
back to the hatch before **22:00**. Miss the deadline and the night arrives with
more of them than you can ever kill.

Three save slots, and the interface is available in English and Spanish (picked on
first launch, changeable any time from the main menu).

## Building (Windows, MSYS2 UCRT64)

Needs the Steamworks SDK unpacked in `external/sdk` (for co-op) and these MSYS2 packages:

```
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-openal mingw-w64-ucrt-x86_64-libvorbis
```

(MSYS2 installed in `C:\msys64`, which `build.bat` expects.) Then either:

```
build.bat            :: full build into bin\Outbound.exe (+ DLLs and every asset it uses)
sh build/make.sh     :: incremental build for development (also copies the assets)
package.bat          :: build, then zip it all into dist\Outbound_0.8v.zip to share
```

Both builds copy exactly what the game loads at runtime into `bin\`: every sprite
(`assets/sprites/**/*.png`, without the Gif preview folders), the sound effects
(`assets/sounds/*.wav`), the music (`assets/Music/*`) and `credits.txt`. So `bin\`
on its own is the whole game: zip it (or run `package.bat`) and it runs anywhere.

Run `bin\Outbound.exe`. The game looks for `assets/` next to the exe first, then one
or two folders up. Saves go in a `saves/` folder next to the data; a development build
in `bin\` keeps using the project's `saves/` folder one level up.

## Controls

| Key | Action |
| --- | --- |
| WASD | Move |
| Shift | Sprint (uses stamina) |
| Mouse | Aim |
| Left click | Shoot |
| R | Reload |
| 1 / 2 / Q / wheel | Switch weapon (the wheel picks from the interaction list instead when several things are in reach) |
| G | Throw grenade |
| H | Quick heal |
| L | Toggle laser pointer (on a gun the crafter fitted one to) |
| E | Interact, search containers, enter the bunker; with several things in reach, acts on the one selected in the list |
| Tab | Inventory |
| M | Map |
| Esc | Pause / close panel |
| F11 | Fullscreen |

Xbox-style controllers are supported: left stick/D-pad moves, left-stick click
sprints, right stick aims, RT shoots, A interacts/selects, X reloads, Y heals, LB
throws a grenade, RB switches weapons, Back opens inventory, B opens the map/mission
view, and Start pauses. Aiming with the stick has aim assist: it locks onto a visible
hostile near where you point, and picks one up in front of you when the stick is let go.

Every menu and panel works with the controller too: the D-pad or left stick jumps
between buttons and item slots, A selects, X does what a right click does (use or
equip an item), B goes back, and left/right adjusts a focused slider. The right stick
still moves a free cursor (handy for placing turrets and scrolling the credits).

Every keyboard key can be rebound in **Controls** (main menu or pause menu): click the
key next to an action and press the new one. Bindings are saved with the other settings.

## Options

**Options** (main menu, and both pause menus) sets the music and sound-effect volumes
and the screen mode: windowed, fullscreen or borderless. F11 still switches between
windowed and full screen. Settings are kept in `saves/settings.txt`.

On-screen hints always show the real key, mouse button or controller button for an
action (Kenney's Input Prompts), and switch the moment you pick up a controller.

## Weapon tiers

Guns come in Fortnite-style tiers. The trader always sells the ordinary **Uncommon**
(green) version, which works exactly as guns always have. Guns found in the world or
dropped by raiders roll a tier; better loot rolls better tiers. The days gate it: nothing
epic turns up before day 3 and rare guns are scarce at first (about 6% of guns on day
1), the odds reach their full range around day 8, and elite guns start on day 4.
However far you walk, the first days' loot stays modest.

| Tier | Colour | Damage | Spread | Reload | Sells for |
| --- | --- | --- | --- | --- | --- |
| Common | grey | -12% | +15% | +10% | x0.8 |
| Uncommon | green | normal | normal | normal | x1 |
| Rare | blue | +10% | -8% | -5% | x1.3 |
| Epic | purple | +22% | -15% | -10% | x1.7 |
| Legendary | gold | the elite guns, on their own stats | | | |

## Elite guns and bleeding

Now and then a gun found out in the world is an **elite** one: a better version of a
normal gun, never sold by the trader. They have a gold frame and name, an ELITE tag on
the HUD, and a gilded look in your hands.

| Elite | Better version of |
| --- | --- |
| M92, Luger, .357 Magnum | Pistol (the Magnum: Revolver) |
| MP5 | SMG |
| M15 | Carbine |
| AK-47 | Assault Rifle |
| M24 | Sniper Rifle |

The **crafter** in the bunker works on one gun at a time: a tier up (money and gun
parts, up to epic), a **laser pointer**, an extended and then a drum magazine. A fitted
laser has its own on/off switch: **L** toggles it on the gun in your hands. Better tiers throw a longer beam, and
the beam stops on walls, doors, trees and cars like the flashlight does.

Getting shot can make you **bleed** (7% per bullet, half as likely with armour on). It
drains health slowly for about a minute unless you use a bandage or medkit, and it never
takes you below 10 HP on its own.

## How a run works

1. **The bunker** — sleep (saves and heals), stash loot, sell at the trader,
   buy gear, and spend money on upgrades at the workbench.
2. **Head outside** — each day gets its own world: terrain, buildings, loot,
   enemies, cars, everything. Ducking back into the bunker and heading out again
   on the same day returns you to the same place; sleeping rolls a new one.
3. **Loot and fight** — raiders hold the towns and camps; loot is better the
   further you get from home.
4. **Get home** — the bunker hatch closes at 22:00. After that the night horde
   spawns endlessly and hunts you down. Dying costs you everything you carried.

Buildings are roofed, so you cannot see inside until you step through the door —
the roof fades out while you are under it, and falls in for good once enough of
the walls have been shot away. A door in the south wall faces the camera and is
never covered, so it is drawn as a real door; every other way in — a door in a side
or back wall, or a hole blown through one — is marked with a small chevron pointing
the way through, since the roof is hiding it.

Within a day the world remembers what you did: where you explored, who you killed
and which containers you emptied all persist if you duck back into the bunker and
head out again. Sleeping rolls a new world and clears it — and every day out is
deadlier than the last, with tougher, more numerous enemies that hit harder, so a
day you have already cleared is worth coming back to.

## Hordes and the bunker's defenses

Roughly every day and a half to two days, at any hour, a zombie horde spills
out of a corner of the map and heads for the hatch. Zombies outrun you on foot, so
when the radio calls one in (two hours out, on the HUD) it is time to get home. They
path across the whole map, bash through fences, and tear into anything in the way:
you, your hired guns, your turrets, and finally the bunker itself. Every horde is
bigger and tougher than the last. Each kill pays a bounty, and repelling a horde pays
a bonus; if the bunker is overrun they take a quarter of your money. A horde due on
a day you sleep through is fought out without you by your turrets and guards. Once a
horde is on, the hatch stays shut until it is beaten, so there is no ducking below
mid-fight. At 22:00 every zombie becomes immune to damage: the
bunker is the only safe place at night. The exception is a horde due in the night
(22:00 to 06:00): you cannot sleep through it. The night stays dark but the dead stay
killable, and once it is beaten the rest of
the night is quiet and the bed is yours. The bed can also let you wait until just
before it arrives. Die out there and it is an ordinary death.

The **defense console** in the bunker opens a view of the compound outside. Place
turrets there, level each one up to Lv5, repair or sell them, unlock new kinds (Gun,
Auto, Flamethrower, Laser, Rocket, gated by hordes repelled) and research global
upgrades. Turrets persist between days. Sleeping patches the bunker up and restores
half the plating on standing turrets; wrecked turrets need a paid repair.

Its **Walls** tab builds barricades from the art pack's buildable wood: wooden walls
and gates, and reinforced ones once a horde has been repelled. Hold the button and
drag to lay a line. The walls join up at corners and T-junctions. Gates swing open
for you, your mercenaries and your friends, and shut behind you. The dead bash
through a wall when going round it would take longer, and a reinforced wall holds them
a long time. Broken barricades are repaired or sold from the same tab. The compound's
back fence now has wire gates (the middle one locked) that open the same way.

The **recruiter** hires up to three mercenaries. The more you pay, the better their
gun, health and aim. Set each one to follow you outside or guard the compound. A
hireling who dies is gone for good, and their gun is left on the body.

## Fighting up close

**F** (a tap of **R3** on a controller; holding R3 toggles the laser) hits whatever is
right in front of you: a punch, or a swing of the **baseball bat** if you carry one in
your pockets (the trader sells them, and they turn up in the world). It costs a little
stamina, knocks the target back and puts it off its stroke. Mercenaries punch the dead
off when they get too close to shoot.

The axe zombies throw their axes at anyone a little way off, then fight bare-handed
until they pick them back up.

## Weather and colour

The sky changes on its own every couple of hours: clear, hazy, overcast, drizzle,
rain, storms with lightning, drifting fog, and mixes like rain with fog. Some days
are wetter than others. Rain leaves puddles on open ground that splash when you walk
through them and dry out slowly afterwards; nothing gets wet under a roof. The
picture is graded by the time of day (pink dawn, golden evening, blue night), by a
mood rolled for each day's world, and turns red while a horde is on. Every change
eases in rather than switching.

Terrain is destructible: bullets chew through walls and trees over several hits,
explosions clear everything in their radius.

## Assets

Artwork and sound come from the packs listed in `credits.txt`, which the in-game
credits screen reads at runtime:

* Post-apocalypse pixel art pack — The Lazy Stone
* Survival Effects — Darkworld Audio
* Anemoia (ambient music) — Rusted Studio

Everything under `assets/sprites/` is loaded automatically at startup:

* `Name-Sheet6.png` is treated as a 6 frame animation strip.
* `Name_TileSet.png` is cut into 16x16 tiles, and tiles that tile seamlessly are
  sorted into ground types (grass, asphalt, soil, interior, brick...) by colour.
  That sorting is only used for interiors and rubble now: picking ground by colour
  alone kept choosing sidewalk stripes and wood floors as though they were fields.

The outdoor ground is addressed explicitly instead. The three `Background_*_TileSet`
sheets share one 24x17 layout, so `src/art.cpp` names the tiles it wants by frame
index (`frame = row * 24 + column`): grass, bare earth, asphalt, and the corner
("Wang") set at columns 6-8 / rows 12-14 plus the inner corners at columns 8-9 /
rows 15-16. Those corner tiles are what joins grass to bare earth along a proper
ragged seam rather than a straight cut. Each sheet carries its own grass, earth and
seam art, so the lush and dry biomes each autotile within their own sheet. Roads get
a one tile dirt shoulder at generation time so asphalt never meets grass directly.
* Everything else is a single sprite, looked up by its lowercased path, e.g.
  `objects/nature/green/tree_5_big_green`.

The same sheets also draw kerbs, and the game uses them (0.12v): each Background sheet
has a kerbstone edge for its paving (frames 6-10, 30-34, 56-58), its grass (11-13,
35-37, 59-61, 120-121, 144-145) and its earth (19-21, 41-45, 65-69). Paving next to a
road gets the kerb on its road side, and a planter (a tile with `TF_KERB`) gets one
wherever it ends, so the asphalt stays plain right up to the kerb. The Bleak-Yellow
sheet is the dead grey scrub (`G_WASTE`); it draws its own edges against green grass
(its earth ring's frames -3, inner corners -2), dry grass (+3 / +2) and earth.
Road paint (zebra crossings 169-171 / 216-264, parking bays 193-244), heaps from
`Garbage_TileSet` and grass creeping over paving from `Grass_On-Top_TileSet` are laid
over the ground as `Tile::overlay` (see `Art::Overlay`).

Everything that makes the world look lived in is added last, by the dressing pass in
`src/dress.cpp`, on dice of its own so it never moves the day's buildings, loot or
raiders: vegetation colours in stands (`Tile::tone`), ground detail (`Tile::worldDeco`,
see `Art::DecoKind`), road paint and garbage, street furniture and clutter
(`PROP_OBJECT`, which block with their pixels like cars; see `Art::ObjectKind`),
windows, posters and graffiti on the front walls (`PROP_WALLDECO`), and what stands on
the flat city roofs (`World::roofProps`). `tools/make_flat_roof.py` makes the plain
roof concrete it uses.

The interface is drawn from the pack's own UI sheets (0.12v, `src/ui.cpp`): panels are
`UI/Inventory/Inventory_1` cut in nine (corners kept, edges repeated, middle
stretched), buttons the beige `Main Menu/Blank` (the lettered Play, Load, Save,
Settings and Quit where the label is theirs, in English), item cells
`Inventory-Cell` / `Inventory-Chosen`, sliders the menu `Scrollbar`, check boxes
`Checkmark`, confirmations `Button_Yes` / `Button_No`, recipes the `Crafting` strips,
and the HUD the `HP`, `Hunger`, `Bullet Indicators` and `Quick-Access-Inventory` art.

Add or replace a PNG and it appears on the next launch — `src/art.cpp` is the one
file that maps game concepts (trees, cars, guns, zombies, item icons) to those
paths. Sounds work the same way: `assets/sounds/<name>.wav` overrides the
built-in synthesized effect (see `SOUND_LIST` in `src/audio.h`).
Anything the pack does not cover falls back to small procedurally drawn sprites.

## Source layout

| File | Purpose |
| --- | --- |
| `main.cpp` | window, game loop, dev flags |
| `assets.cpp` | scans `assets/sprites`, packs one texture atlas, classifies tiles |
| `art.cpp` | maps game concepts to sprites in the pack |
| `render.cpp` | sprite batching, world/glow/UI layers, lighting composite |
| `world.cpp` | world generation, collision, line of sight, pathfinding field |
| `dress.cpp` | the dressing pass: vegetation colours, ground detail, street furniture, roofs and fronts |
| `raid.cpp` | the outside world: player, AI, bullets, the night |
| `base.cpp` | the bunker and its stations, the recruiter |
| `defense.cpp` | turret/mercenary tables and the defense console editor |
| `game.cpp` | shared state, depth-sorted drawing, save/load |
| `items.cpp`, `inventory.cpp`, `ui.cpp`, `menu.cpp`, `audio.cpp` | as named |

Developer flags (handy while working on the game):
`--raid`, `--base`, `--inv`, `--enemies`, `--atbuilding`, `--time=MINUTES`,
`--mission`, `--horde` (raid with a horde launched at once and one of each turret),
`--squad` (two hirelings), `--zombies`, `--defense`, `--recruit`, `--sleephorde=N`
(times horde N fought offscreen and quits), `--weather=N` (hold one weather: 0 clear,
1 hazy, 2 overcast, 3 drizzle, 4 rain, 5 storm, 6 fog, 7 rain + fog, 8 overcast + fog),
`--shot=FILE@SECONDS`, `--screen=lang|slots|intro|credits|controls`, `--seed=N` (with
`--raid`: the same world every run), `--at=X,Y` (with `--raid`: start on that tile),
`--barricades` (with `--raid` or `--defense`: a ring of walls and gates round the
bunker), `--searching` (with `--raid`: the nearest container open, still being
searched), `--bot` (walks in a circle, shooting and punching what comes close),
`--local=N` (with `--raid`: N local co-op players).
Set `OUTBOUND_DRESS_LOG=1` to have the city blocks and buildings listed with their
tile coordinates, handy with `--at` for looking at a particular kind of place.

`--raid` and `--base` start a throwaway game and never write to a save slot.

Interface strings live in `src/lang.cpp`, keyed by their English text, so an
untranslated string still shows up in English rather than as a missing key.

## World tile selector

To inspect or change the tile used by any world-rendering case, run
`tools\run_tile_selector.bat`. The editor shows the real spritesheet at a
numbered grid, groups every connected-wall/ground/roof/facade case, and marks
the current tile. Click a tile, then choose **Save & Build game**. The editor
updates `assets/tile_mappings.json`, regenerates `src/tile_mappings.generated.h`,
and rebuilds `bin\Outbound.exe`.

The right-hand pane is a live representative scene — a grass field, a dirt
patch with its transition seams, a road crossing, a fenced compound, a brick
wall, and a building under both roof styles — redrawn from the current
mappings the moment you click a tile or change the palette, so you can see
how a choice reads in context before committing it.

Use **Launch / restart game** to keep a game preview open beside the selector.
After each save/build, the preview is restarted automatically with the new
tileset mappings.

## Saving

The game saves whenever you sleep, come home or change something in the bunker, and
every 30 seconds while you are outside. Closing the game mid-raid (or **Save & quit to
menu** from the pause screen) keeps the raid: Continue puts you back outside at the same
time and place, with what you carried, what you looted and who you killed. Only
**Abandon raid** throws the trip away.

## Local co-op

Up to four players on one screen. **Local co-op** on the main menu opens the lobby:

* A controller (Xbox or PlayStation) joins with **A / Cross** or **START / OPTIONS** and
  leaves with **B / Circle**; the keyboard and mouse join with **ENTER** (or the *Join
  with keyboard* button) and leave with **BACKSPACE**. Left and right pick a shirt.
  Every place shows its own device's buttons.
* Player 1 is whoever joined first and plays the save's own character; they start with
  **START / ENTER** once there is a second player, then pick the save slot.
* In the game a free controller can still drop in with **START** (or the keyboard with
  **ENTER**), and a player drops out from their pause menu.

Everyone shares the world and one camera that follows the group and pulls back (as far
as the *co-op zoom* option lets it) to keep everyone in view; nobody can walk off it.
Each player has a card along the bottom (health, stamina, gun and magazine, grenades
and medicine), their own prompts in their own buttons, and their own pointer in their
colour; controllers aim with the right stick, with aim assist. One player works a menu
at a time. Stairs, the catacombs and the hatch take the whole group. A player who
bleeds out waits in the bunker until the others come home; if everyone bleeds out the
day ends for all.

## Co-op (Steam)

Up to eight players share one host's world. Pick **Co-op** in the main menu, then
**Host a game** and a save slot; that opens a lobby. **Invite** opens the Steam overlay
to invite friends (they can also right-click you in their friends list and choose
Join Game). The host presses **Start**; anyone invited later drops straight in.

* The host's save holds everything: the world, the bunker and turrets, and every
  guest's character (kept in `saves/outboundN_coop/`). Guests' own saves are untouched.
* Each player has their own money, missions, upgrades and mercenaries, and can be in
  the bunker or outside independently. The clock runs while anyone is outside; with
  everybody in the bunker it stops, as in single player.
* The bunker's big stash is shared by everyone (one player works in it at a time, so
  nothing can be taken twice); each player also has a small private stash.
* More players, more enemies (raiders and horde zombies): 1-2 players as solo, 3-4
  players +10-15%, 5-6 another +10%, 7 another +10%, 8 another +5%. Their damage stays
  the same.
* Teammates outside the screen are marked at its edge with a small arrow, their name
  and distance; a downed teammate's marker blinks red.
* Everyone has to be in bed to sleep through to morning. If the night runs out with
  nobody asleep, the new day starts anyway.
* When a horde comes, everyone in the bunker is sent outside to fight it. Nobody can
  sleep through one: if it is due before morning, sleeping only lasts until it lands.
* A player at 0 HP goes down instead of dying. A teammate holds **E** next to them
  to revive them within 30 seconds; otherwise they bleed out. Everything they carried
  stays on their body, and they wake up in the bunker with a pistol.
* Every player can use the defense console (they pay from their own money). Everyone
  has a colour and a name tag.

Development builds use Steam's test app (App ID 480, `bin/steam_appid.txt`); a
release needs its own App ID. Ship `steam_api64.dll` next to the exe. For testing on
one PC without Steam: `Outbound.exe --lan-host` and `Outbound.exe --lan-join`.
