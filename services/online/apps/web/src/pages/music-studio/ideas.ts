import { t } from '../../i18n.tsx';

// Resolve UI suggestions at render time so language switching also updates the
// text inserted into the composer. Keep the existing English titles as keys.
export const musicIdeas = () =>
  [
    [
      'Moss & morning light',
      t('Moss & morning light'),
      t(
        'A warm woodland piece with flute, harp and gentle pizzicato. Let the melody wander and return; combat adds quiet drums and a bassoon counterline.',
      ),
    ],
    [
      'Little clockwork garden',
      t('Little clockwork garden'),
      t(
        'Soft glass bells and rounded synth pads, curious and unhurried. A playful answering melody, with a deeper pulse as the colony grows.',
      ),
    ],
    [
      'A waltz for the rain',
      t('A waltz for the rain'),
      t(
        'A cozy folk waltz with recorder and kalimba. Sparse, breathing phrases for calm, a lilting rhythm for building, and warm low strings for combat.',
      ),
    ],
  ] as const;
