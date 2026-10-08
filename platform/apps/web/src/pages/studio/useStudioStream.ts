import { createStudioStream } from '../../components/studio/useStudioStream.ts';
import { ROOT, mergeThread, type Thread } from './types.ts';
export const useStudioStream = createStudioStream<Thread>(ROOT, mergeThread);
