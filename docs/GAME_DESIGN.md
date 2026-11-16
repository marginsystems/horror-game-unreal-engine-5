# Game design

Working title options: **Warranty Void**, **Dud**, **Batteries Not Included**, **Out of Order**, **Misfire**.

## The pitch

A first-person horror game where the monster is real, the dread is real, and every weapon you
pick up is a defective piece of junk. The horror is played straight. The comedy comes from you:
the under-equipped employee of the cheapest monster-removal contractor in town.

The rule that makes it work: **weapons never kill anything, but every failure does something
useful.** A jammed pistol makes a loud click that lures the monster. A confetti shotgun makes a
mess the monster has to stop and look at. The player learns to use broken tools on purpose.

## The loop (first playable)

1. You are dropped into one location at night with a flashlight and a clipboard.
2. Find 5 fuses to power the exit shutter.
3. One monster hunts you. It patrols, investigates noise, and chases on sight.
4. Weapons are scattered around. None of them hurt the monster. All of them change what it does.
5. Reach the exit to win. Get caught and you get a death screen with a cause of death
   ("Trusted the shotgun").

A round should take 5 to 10 minutes.

## Setting options

Each one maps onto free environment packs that already exist (see [ASSETS.md](ASSETS.md)).

| Option | The place | Why it is funny | Free kit |
| --- | --- | --- | --- |
| **A. Night clinic** | A closed hospital wing with one flickering ward | Waiting room ticket machine still works; the monster takes a number | Horror Hospital, Modular 3D hospital |
| **B. Drive-in cinema** | An abandoned drive-in with the projector still running | The monster stops to watch the film; you sneak past in front of the screen | Abandoned Drive-in Cinema bundle |
| **C. Bus depot, last stop** | A dead bus depot at 3 a.m. | Timetable board announces the monster's arrival, always "delayed" | Abandoned Bus Depot bundle |
| **D. Office backrooms** | Endless yellow office space | Motivational posters, a printer that never stops, a monster in a lanyard | (PSX) Backroom, Backrooms audio packs |
| **E. The mine** | A flooded mine with a lift that needs fuses | Canary in a cage that heckles you | Abandoned Mine bundle, Underground Tunnel |

Recommendation: **A, the night clinic.** It has the best free environment coverage, corridors
and doors make monster AI easy to get right, and every room type gives a new joke.

## Monster options

Every monster has one scary behaviour and one stupid weakness. The first playable needs one.

| Monster | Scary part | Stupid part | Free model |
| --- | --- | --- | --- |
| **The Walker ("Gary")** | Tall, thin, only moves while you are not looking at him | Freezes mid-step in ridiculous poses; apologises in a whisper when he bumps a door | The Hollow Walker (CC-BY, animated) |
| **The Librarian** | Blind, hunts by sound, runs at anything that makes noise | Stops to shush every noise first, including her own footsteps | Monster with Book Head |
| **Honks** | A clown thing you hear long before you see it | The honk gets quieter the closer it is, so silence is the warning | Free clown monster |
| **The Mouth** | Fast, all teeth, chases in a straight line | Cannot turn corners; slides into walls and has to reverse out | Mouth Monster |
| **The Intern** | Looks harmless and small, follows you everywhere | It is the only one that can actually open doors, and it holds them for the others | Cute Creature |
| **The Patient** | Classic shambling mutant in the corridor | Obeys hospital signage: stops at "wet floor" signs, queues at reception | Zombie Mutant, Flesh Mutant Monster |

Recommendation for the first playable: **The Walker**. "Moves only when unobserved" is simple to
build, reliably scary, and the frozen poses are the joke. Its model is CC-BY, so it can live in
the open source repository.

## Weapons that do not work

| Weapon | What you expect | What happens | What it is good for |
| --- | --- | --- | --- |
| **Pistol** | Bang | A "BANG!" flag pops out of the barrel; pull the trigger again to wind it back in | The click lures the monster to where you were |
| **Shotgun** | Stopping power | Confetti and a party horn | Monster stops for a few seconds to stare at the confetti |
| **Rubber chicken** | Melee | It squeaks | Throw it: every bounce squeaks, and the monster chases the squeaks |
| **Taser** | Stun | Shocks you; screen wobbles, you drop what you hold | Short speed boost while your legs panic |
| **Grenade** | Explosion | Nothing. Dud | It goes off about three minutes later, wherever it ended up. The only weapon that works |
| **Holy water spray** | Banishment | Monster is now damp and annoyed, like a cat | It walks off to shake itself dry |
| **Flashlight** | Light | Dies constantly; smack it to get a few more seconds | Smacking is loud |
| **Chainsaw** | Carnage | Pull cord, pull cord, pull cord. It never starts | Every pull is a loud noise you can place on purpose |

First playable needs three: pistol, shotgun, rubber chicken.

## Funny mechanics

- **Customer support hotline.** Wall phones call the weapon manufacturer. You get hold music.
  The monster likes the hold music and walks to the phone, which buys you time elsewhere.
- **Involuntary scream.** Look at the monster too long and your character screams on their own.
  A "composure" meter refills in the dark with your eyes off it.
- **Warranty cards.** Each weapon comes with a card. Collect them for one-line lore
  ("Not for use against the undead, the living, or at all").
- **Cause of death screen.** Blames the last item you used. "Cause of death: read the manual."
- **Incident report.** The win screen is a form your character fills in, scoring you on
  fuses found, weapons "discharged" and screams.
- **The monster has manners.** It knocks before entering a room. It still comes in.
- **Doors.** You can only close a door gently. Slamming is not in your contract.

## Build order

1. Grey-box level: corridor loop, four rooms, exit shutter.
2. First-person character: walk, sprint, crouch, flashlight, interact.
3. Fuse pickups and the exit shutter.
4. Monster AI: patrol, hear noise, investigate, chase, catch.
5. Noise system: every weapon and action emits noise the monster can hear.
6. Three weapons.
7. Death screen and win screen.
8. Swap grey boxes for the environment kit, add sound and lighting.
