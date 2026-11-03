// Shared authoring contracts. Each studio keeps its own product, wallet and settings.
import { Type } from 'typebox';
import { Strict, Uuid } from './common.ts';

export const StudioCreditPack = Strict({
  id: Type.String({ minLength: 1, maxLength: 64 }),
  priceId: Type.String({ pattern: '^price_' }),
  credits: Type.Integer({ minimum: 1 }),
  amount: Type.Integer({ minimum: 1 }),
  currency: Type.Union([
    Type.Literal('usd'),
    Type.Literal('cad'),
    Type.Literal('eur'),
    Type.Literal('gbp'),
  ]),
});

export const StudioCreate = Strict({
  title: Type.String({ minLength: 1, maxLength: 128 }),
  /** Lets a prompt-first client recover an interrupted project-creation response. */
  id: Type.Optional(Uuid),
});
export const StudioMessage = Strict({
  id: Uuid,
  text: Type.String({ minLength: 1, maxLength: 8000 }),
});
