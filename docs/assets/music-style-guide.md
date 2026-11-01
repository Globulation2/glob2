# Soundtrack style guide

This guide records what makes a soundtrack set work in Globulation 2. It is based on
candidate sets that a maintainer judged by listening and that were also measured. Read it before composing, adapting or generating a new
set. The tools that build and check sets are described in the
[music pipeline guide](music-pipeline.md).

Taste decides, and measurements only support it. A set that passes every automatic
check can still be wrong for the game. The initial sets were approved by a
maintainer who listened to them; revisions need renewed listening in gameplay.

## How the game uses music

A set is three loops of equal length in `data/zik/<set>/`. `a1.opus` is calm,
`a2.opus` is building and `a3.opus` is combat. The mixer (`src/audio/SoundMixer.cpp`)
switches mood by crossfading into another file at the same playback position, and
every file loops forever.

The fade lasts about 0.37 seconds. Judge transitions at that duration with the
pipeline's default preview and in gameplay, including returns to calm and changes
during sparse passages. Matching whole-song LUFS does not prevent local jumps.
The `transition` QA check warns about those jumps, but it cannot judge feel.

- **The three moods are one piece.** They must share tempo, beat grid and
  harmony, so that a crossfade at any moment sounds like the arrangement changing,
  not like a different song. New shipped sets must use one explicit score
  orchestrated three ways, with direct control over every note and instrument.
- **Each file must loop seamlessly.** The wrap needs no click and no gap, and
  reverb tails should carry over it. The last bar should lead naturally into the
  first, for example a dominant chord resolving onto bar 1.
- **It will be heard for a long time.** A match lasts tens of minutes and a loop
  is 1–2 minutes, so material that charms once can grate on the twentieth pass.

## What has worked

The maintainer approved these:

| Set | Approach | Why it works |
|---|---|---|
| Moss Lanterns | An original score played on CC0 orchestral samples | Melody with counter-lines, a section that develops and returns, and a dynamic arc. Woodwinds, harp and glockenspiel feel whimsical without being childish |
| Thistle Waltz | An original score played on CC0 folk samples | Recorder, kalimba and folk harp give it an organic, handmade character, and the 3/4 time keeps it light |
| Glass Garden | An original score played on synth sounds designed for it (Surge XT), heavily filtered | A soft FM bell lead over a dark, filtered pad. Electronic, but warm and smooth |
| Bramble Jig | An original score in 6/8 on CC0 samples | Flute, clarinet and glockenspiel over pizzicato strings. Combat is a real re-arrangement (galloping low riff, horns, drum line), not just louder |
| Fennel Mist | An original slow pastoral score (E Dorian, 72 BPM) on CC0 samples | Bassoon and horn with a flute descant and harp. Misty and unhurried. Combat hands the tune to horn over driving strings |

Woodland, Apple Cider and Curious Critters were subsequently withdrawn after
listening in gameplay. Their transitions did not preserve the intended feel,
including after attempts to improve continuity. They are not approved references
for future sets. The original soundtrack remains the benchmark for feel.

**The target feel is downtempo:** the relaxed, unhurried music of "chill" or "cozy"
games, not action-game scoring. Broadly, it is the calm, pleasant background music
common in simulation and city-building games (SimCity, for example) and in cozy
games such as Animal Crossing: light, warm, often a little jazzy or folksy, and easy
to hear for hours. These are references for the general mood, not models to copy;
never imitate any game's melodies or arrangements. Even combat should stay inside that world: more
urgent and weightier, but still warm and never aggressive.

What these have in common:

- **Warm, rounded tone.** Woodwinds, plucked and struck instruments, strings, soft
  pads and filtered synths. The mean spectral centroid (a measure of brightness) of
  approved calm moods sits at about 0.7–1.5 kHz, and building and combat at about
  1.1–2.3 kHz; the original set spans 0.9–1.7 kHz. Very little energy sits above
  10 kHz.
- **Gently playful, not childish and not epic.** The game is about cute,
  organic globules, so the music should be whimsical, cozy and a little curious.
  Trailer-style epic, historical war music and comedy cartoon music all miss.
- **Written or performed music.** Each piece has a real melody, an answering
  line, harmony that moves, and phrases that develop and return. Loudness and
  timing vary like a human performance. Notes are not all equally loud or locked
  to the grid.
- **Room to breathe.** The texture has space between phrases and stays out of
  the way of game sound effects, without dropping into near-silence.
- **Real contrast between moods on one timeline.** Calm drops the drums and
  thins the arrangement. Building adds rhythmic drive. Combat adds percussion,
  low end and urgency while keeping the same tune and key.

## What has not worked

Rejected by ear, with the reason heard and what the measurements showed:

| Rejected | What was heard | What was measured |
|---|---|---|
| Woodland, Apple Cider and Curious Critters (adapted recordings) | Mood transitions did not preserve the desired gameplay feel | Local jumps could be reduced numerically, but the continuity remixes were still rejected by listening |
| General MIDI renders of note lists coded in Python (the first generated sets) | Loopy, cheap, "AI slop" | About 20 s of material repeated 3–4 times per file (repetition r 0.97–1.00). Every note equally loud, notes locked to the grid, stock GM instrument sounds, four-chord vamps |
| Steel drums, lounge electric piano and other genre clichés | Don't fit the game | Taste, not measurable |
| Toy Soldiers | Busy and bright | 5.5+ onsets per second and 2.4–2.8 kHz brightness. Calm still busy |
| Unfiltered synths (early Surge renders of Moss Lanterns and Glass Garden) | Scratchy, with background noise | A constant hiss above 6 kHz, only 3–7 dB below its median even in quiet passages, from raw saw and pulse waves |
| Orchestral Dawn (ACE-Step 1.5 + Demucs) | Disharmonious during gameplay; removed after in-game listening | Automated checks did not capture the perceived harmonic problems |
| ACE-Step folk prompts | Don't fit Glob2's feel | Muddy low-mid mix, flat loudness, a uniform "produced song" texture |

## Practical rules

1. **Start from music, not from a loop.** Write a through-composed piece
   of 50–120 s. Repeating a short bar pattern is the fastest route to "AI slop".
2. **Arrange the three moods from one score.** Use explicit note sequences with
   shared timing and controlled instruments, rendered with samples or synths.
   Decomposing a finished recording does not give enough control over the
   arrangements and their transitions; do not use that method for new shipped sets.
3. **Keep the sound clean.** Filter synths; the first synth renders failed because their
   filters were never switched on. Avoid broadband noise layers, aliasing waveforms and
   added treble boost. Check the quiet passages for hiss.
4. **Keep calm audible.** Its quietest stretches should stay within about 10 dB of
   its average, or it disappears under game sounds.
5. **Make the moods clearly different, not just louder.** A listener should hear
   the change within a bar or two. The first Curious Critters adaptation was judged "OK, but the variants sound too alike":
   its percussive share went only 0.02 → 0.03 → 0.09. Combat needs low end and
   percussion, and ideally its own figures (riffs, ostinatos), on the same timeline.
   Calm should strip back to a lead and a soft bed.
6. **Keep authorship and rendering controllable.** Shape the composition as a
   score that can be edited note by note. Use pinned, licensed samples or designed
   synth sounds. Finished-song generation and stem separation are not supported
   authoring paths.
7. **Normalise loudness consistently.** Aim for about −18/−17/−16 LUFS for
   calm/building/combat, with a true peak of at most −1 dBTP, so sets sit at similar
   levels and switching sets is not jarring.
8. **Let a person listen.** Nobody hearing it in game means it isn't done. Put
   candidates on an audition page with in-sync mood switching before shipping.

Related: [asset production](README.md).
