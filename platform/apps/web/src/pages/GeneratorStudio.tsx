import { useLocale } from '../i18n.tsx';
import { CodingStudio } from './CodingStudio.tsx';
export function GeneratorStudio({ id }: { id?: string }) {
  useLocale();
  return <CodingStudio id={id} generator />;
}
