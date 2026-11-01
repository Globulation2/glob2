import type { FastifyInstance } from 'fastify';
import { codingStudioRoutes } from '../coding-studio/routes.ts';

export function aiStudioRoutes(app: FastifyInstance) {
  return codingStudioRoutes(app, 'aiStudio');
}
