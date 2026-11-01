import type { FastifyInstance } from 'fastify';
import { codingStudioRoutes } from '../coding-studio/routes.ts';

export function generatorStudioRoutes(app: FastifyInstance) {
  return codingStudioRoutes(app, 'generatorStudio');
}
