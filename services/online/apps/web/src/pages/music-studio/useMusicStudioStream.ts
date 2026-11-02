import { createStudioStream } from '../../components/studio/useStudioStream.ts';
import { ROOT, mergeThread, type Thread } from './types.ts';
export const useMusicStudioStream = createStudioStream<Thread>(ROOT, mergeThread);
